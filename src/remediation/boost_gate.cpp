#include "remediation/boost_gate.h"

#include "core/json_persistence.h"
#include "remediation/backup_store.h"
#include "remediation/remediation_service.h"
#include "remediation/target_discovery.h"
#include "remediation/windows_state_api.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace gno {
namespace remediation {
namespace {

std::optional<ActionTarget> ifaceTarget() { return discoverPrimaryInterface(); }

std::optional<DiscoveredGame> gameTarget() { return discoverRunningGameProcess(); }

std::string readMarker(const std::string& path) {
    std::ifstream f(path);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

void writeMarker(const std::string& path, const std::string& value) {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream f(path, std::ios::trunc);
    if (f) f << value << "\n";
}

std::string actionName(ActionId id) { return to_string(id); }

} // namespace

std::vector<ActionId> boostActionsFor(const BoostRequest& req, bool has_game,
                                      bool has_interface,
                                      std::vector<std::string>* skipped_out) {
    auto skip = [&](const std::string& reason) {
        if (skipped_out) skipped_out->push_back(reason);
    };
    std::vector<ActionId> ids;
    if (req.optimize_power_plan) ids.push_back(ActionId::PowerPlan);
    if (req.disable_game_dvr) ids.push_back(ActionId::GameDvr);
    if (req.disable_game_mode) ids.push_back(ActionId::GameMode);
    if (req.disable_mouse_acceleration) ids.push_back(ActionId::MouseAccel);
    if (req.disable_fullscreen_optimizations) {
        if (has_game) ids.push_back(ActionId::FullscreenOptimizations);
        // Человеческие имена вместо внутренних id: эти строки показываются
        // пользователю в блоке «Пропущено» как есть.
        else skip(actionDisplayName(ActionId::FullscreenOptimizations) +
                  ": игра не запущена — нужен exe игры как цель");
    }
    if (req.set_high_priority) {
        if (has_game) ids.push_back(ActionId::ProcessPriority);
        else skip(actionDisplayName(ActionId::ProcessPriority) + ": игра не запущена");
    }
    if (req.custom_dns) {
        if (has_interface && req.dns_server == kEngineDnsPrimary) ids.push_back(ActionId::Dns);
        else skip(req.dns_server == kEngineDnsPrimary
                      ? actionDisplayName(ActionId::Dns) + ": сетевой адаптер не найден"
                      : actionDisplayName(ActionId::Dns) + ": движок допускает пару " +
                            kEngineDnsPrimary + "/" + kEngineDnsSecondary +
                            " — другой сервер не изменён");
    }
    if (req.tcp_optimization) {
        // TcpInitialRtt — параметр HKLM\...\Tcpip\Parameters, адаптер ему не
        // нужен. Раньше здесь требовался поднятый интерфейс, и TCP-фикс молча
        // пропускался на машине без активного адаптера.
        ids.push_back(ActionId::TcpParameters);
    }
    if (req.mtu_optimization) {
        if (has_interface && req.mtu_value == kEngineMtuValue) ids.push_back(ActionId::Mtu);
        else skip(req.mtu_value == kEngineMtuValue
                      ? actionDisplayName(ActionId::Mtu) + ": адаптер не найден"
                      : actionDisplayName(ActionId::Mtu) + ": движок поддерживает только " +
                            std::to_string(kEngineMtuValue));
    }
    if (req.optimize_virtual_memory)
        skip("Оптимизация виртуальной памяти: вне белого списка движка — пропущено намеренно");
    return ids;
}

BoostGate::BoostGate(std::string backups_dir) : backups_dir_(std::move(backups_dir)) {}

std::string defaultBackupsDir() {
    return persistence::storageFile({}, "Backups").string();
}

std::string BoostGate::marker_path() const {
    return (std::filesystem::path(backups_dir_) / "boost_transaction.txt").string();
}

bool BoostGate::hasActiveBoost() const { return !readMarker(marker_path()).empty(); }

std::string BoostGate::latestAppliedTransactionId() const {
    JsonBackupStore store(backups_dir_);
    auto hist = store.list();
    if (!hist.ok()) return {};
    // list() уже отсортирован от новых к старым (см. JsonBackupStore::list).
    // Спрашиваем ровно то же правило, что и движок: Applied, но также Failed/
    // Cancelled/RollbackFailed с непустым применённым префиксом. Пока здесь
    // был свой список статусов, частично применённые транзакции «залипали» —
    // кнопка отката их не видела, хотя движок откатить их умеет.
    for (const auto& item : hist.value()) {
        auto rec = store.load(item.transaction_id);
        if (rec.ok() && isRollbackEligible(rec.value())) return item.transaction_id;
    }
    return {};
}

bool BoostGate::hasRevertableChanges() const {
    return hasActiveBoost() || !latestAppliedTransactionId().empty();
}

BoostGateResult BoostGate::applyBoost(const BoostRequest& req) {
    BoostGateResult out;
    WindowsStateApi api;
    JsonBackupStore store(backups_dir_);

    const auto iface = ifaceTarget();
    const auto game = gameTarget();
    std::vector<std::string> skipped;
    const auto ids = boostActionsFor(req, game.has_value(), iface.has_value(), &skipped);
    out.skipped = skipped;

    if (ids.empty()) {
        out.ok = true;
        out.detail = "нечего применять через безопасный движок";
        return out;
    }

    auto apply_ids = [&](const std::vector<ActionId>& id_list) {
        RemediationService service(api, store, &defaultTargetResolver);
        return service.applyIds(id_list);
    };

    auto applied = apply_ids(ids);
    if (!applied.ok() && applied.code() == RemediationError::InvalidTarget) {
        // Действий с доступной целью не нашлось — движок вообще не начинал.
        out.ok = false;
        out.error = applied.code();
        out.detail = applied.detail().empty() ? "движок отклонил применение" : applied.detail();
        return out;
    }

    const auto& outcome = applied.value();
    // Действия, которые движок не стал даже пытаться применить (нет CS2, нет
    // подходящего железа и т. п.), показываем в «Пропущено» с причиной, а не
    // молчим о них.
    for (ActionId id : outcome.unsupported_actions)
        out.skipped.push_back(actionDisplayName(id) + ": не поддерживается на этой машине");
    // Разбираем результаты ВСЕГДА, даже если транзакция вернула ошибку: часть
    // действий могла примениться, а часть — вообще не выполняться. Раньше при
    // ошибке мы выходили раньше и UI не показывал ни одной строки.
    for (const auto& o : outcome.outcomes) {
        if (o.status == ActionStatus::Applied || o.status == ActionStatus::AlreadyConfigured)
            out.succeeded.push_back(actionName(o.id));
        else if (o.attempted)
            out.failed.push_back(actionName(o.id));
        else
            out.skipped.push_back(actionName(o.id) + ": не выполнялось — транзакция прервана");
    }
    // ok = «что-то сделано или делать было нечего». Частичный успех — тоже ok:
    // применённый префикс существует, он откатывается, и трей не должен
    // выглядеть сломанным из-за одного упавшего действия.
    out.ok = applied.ok() || !out.succeeded.empty();
    // Ошибку, возвращённую БЕЗ значения (например, Busy: параллельная операция
    // уже идёт), иначе было бы не видно: outcome тогда пустой, и причина
    // терялась бы — пользователь читал бы «движок не применил ни одного
    // действия» вместо «другая операция уже выполняется».
    out.detail = !outcome.detail.empty() ? outcome.detail : applied.detail();
    out.error = applied.code();
    if (!applied.ok() && !out.succeeded.empty())
        out.detail = "применено частично: удалось " + std::to_string(out.succeeded.size()) +
                     ", не удалось " + std::to_string(out.failed.size());

    // Remember the transaction for revert. applyIds doesn't expose the id via
    // ApplyOutcome, so resolve it as the newest Applied record in the store.
    // status=Applied выставляется и при частичном успехе — иначе применённый
    // префикс нельзя было бы откатить.
    if (outcome.status == TransactionStatus::Applied) {
        auto hist = store.list();
        if (hist.ok()) {
            for (const auto& item : hist.value()) {
                if (item.status == TransactionStatus::Applied) {
                    out.transaction_id = item.transaction_id;
                    out.applied = true;
                    break;
                }
            }
        }
        if (out.applied) writeMarker(marker_path(), out.transaction_id);
    }
    return out;
}

BoostGateResult BoostGate::applyIds(const std::vector<ActionId>& ids) {
    BoostGateResult out;
    if (ids.empty()) {
        out.ok = true;
        out.detail = "пустой список действий";
        return out;
    }
    WindowsStateApi api;
    JsonBackupStore store(backups_dir_);

    RemediationService service(api, store, &defaultTargetResolver);

    auto applied = service.applyIds(ids);
    if (!applied.ok() && applied.code() == RemediationError::InvalidTarget) {
        out.ok = false;
        out.error = applied.code();
        out.detail = applied.detail().empty() ? "движок отклонил применение" : applied.detail();
        return out;
    }
    const auto& outcome = applied.value();
    for (ActionId id : outcome.unsupported_actions)
        out.skipped.push_back(actionDisplayName(id) + ": не поддерживается на этой машине");
    // Как и в applyBoost: разбираем outcome даже при ошибке, иначе частично
    // применённая транзакция выглядела бы как «ничего не произошло» и кнопки
    // автофикса не показали бы ни одной строки.
    for (const auto& o : outcome.outcomes) {
        if (o.status == ActionStatus::Applied || o.status == ActionStatus::AlreadyConfigured)
            out.succeeded.push_back(actionName(o.id));
        else if (o.attempted)
            out.failed.push_back(actionName(o.id));
        else
            out.skipped.push_back(actionName(o.id) + ": не выполнялось — транзакция прервана");
    }
    out.ok = applied.ok() || !out.succeeded.empty();
    // См. applyBoost: у ошибки без значения (Busy) причина лежит в Result,
    // а не в outcome — иначе она пропала бы.
    out.detail = !outcome.detail.empty() ? outcome.detail : applied.detail();
    out.error = applied.code();
    if (!applied.ok() && !out.succeeded.empty())
        out.detail = "применено частично: удалось " + std::to_string(out.succeeded.size()) +
                     ", не удалось " + std::to_string(out.failed.size());
    out.applied = outcome.status == TransactionStatus::Applied;
    if (out.applied) {
        auto hist = store.list();
        if (hist.ok()) {
            for (const auto& item : hist.value()) {
                if (item.status == TransactionStatus::Applied) {
                    out.transaction_id = item.transaction_id;
                    break;
                }
            }
        }
    }
    return out;
}

BoostGateResult BoostGate::revertTransaction(const std::string& transaction_id) {
    BoostGateResult out;
    out.transaction_id = transaction_id;
    if (transaction_id.empty()) {
        out.ok = true;
        out.detail = "откатывать нечего: применённых изменений нет";
        return out;
    }
    WindowsStateApi api;
    JsonBackupStore store(backups_dir_);
    RemediationService service(api, store, {});
    auto rolled = service.rollback(transaction_id);
    out.ok = rolled.ok();
    out.detail = rolled.ok() ? "изменения откатены" : rolled.detail();
    out.error = rolled.code();
    // Маркер активного буста снимаем здесь же: иначе после отката трей
    // показывал бы «буст включён» для уже откаченной транзакции, а
    // revertBoost() пытался бы откатить её повторно.
    if (rolled.ok() && readMarker(marker_path()) == transaction_id)
        std::filesystem::remove(marker_path());
    return out;
}

BoostGateResult BoostGate::revertBoost() {
    BoostGateResult out;
    const std::string marker_txn = readMarker(marker_path());
    // Без активного буста откатываем новейший откатываемый фикс из журнала:
    // иначе кнопка «Откатить последние изменения» была бы мертва после
    // одиночного фикса из «Диагностики».
    const std::string txn = marker_txn.empty() ? latestAppliedTransactionId() : marker_txn;
    if (txn.empty()) {
        out.ok = true;
        out.detail = "откатывать нечего: применённых изменений нет";
        return out;
    }
    out = revertTransaction(txn);
    if (out.ok) out.detail = marker_txn.empty() ? "последнее изменение откачено" : "буст откачен";
    return out;
}

} // namespace remediation
} // namespace gno
