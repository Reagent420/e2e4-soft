#include "remediation/remediation_service.h"
#include "remediation/windows_fix_action.h"

#include <chrono>
#include <sstream>
#include <algorithm>

namespace gno {
namespace remediation {

namespace {

std::string generateTransactionId() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::ostringstream out;
    out << "tx-" << std::hex << now;
    return out.str();
}

RemediationService::TargetResolver defaultResolver() {
    return [](const std::vector<FixAction*>& actions) {
        std::vector<std::optional<ActionTarget>> targets;
        targets.reserve(actions.size());
        for (auto* action : actions) targets.emplace_back(NoTarget{});
        return targets;
    };
}

} // namespace

RemediationService::RemediationService(WindowsStateApi& api, IBackupStore& backup_store,
                                       TargetResolver resolver)
    : api_(api), backup_store_(backup_store), resolver_(std::move(resolver)) {
    if (!resolver_) resolver_ = defaultResolver();
}

std::string actionDisplayName(ActionId id) {
    switch (id) {
        case ActionId::PowerPlan: return "План питания";
        case ActionId::EnergyMode: return "Режим энергии";
        case ActionId::GameDvr: return "Xbox Game DVR";
        case ActionId::FullscreenOptimizations: return "Оптимизация полного экрана";
        case ActionId::TcpParameters: return "TCP параметры";
        case ActionId::Dns: return "DNS серверы";
        case ActionId::Mtu: return "MTU интерфейса";
        case ActionId::ProcessPriority: return "Приоритет процесса игры";
        case ActionId::Cs2MaxPing: return "CS2 фильтр пинга";
    }
    return to_string(id);
}

std::string actionDescription(ActionId id) {
    switch (id) {
        case ActionId::PowerPlan: return "Схема \"Высокая производительность\" снижает задержки DPC на ноутбуках";
        case ActionId::EnergyMode: return "Энергопотребление против производительности";
        case ActionId::GameDvr: return "Фоновая запись клипов вызывает микрофризы - отключаем";
        case ActionId::FullscreenOptimizations: return "Отключает вмешательство Windows в полноэкранный swapchain";
        case ActionId::TcpParameters: return "InitialRtt 5000 мс ускоряет установку игровых соединений";
        case ActionId::Dns: return "Быстрые резолверы 1.1.1.1/1.0.0.1 вместо провайдерских";
        case ActionId::Mtu: return "MTU 1500 убирает фрагментацию пакетов";
        case ActionId::ProcessPriority: return "AboveNormal для процесса игры против фоновых задач";
        case ActionId::Cs2MaxPing: return "Официальный фильтр подбора CS2: максимальный пинг серверов матча";
    }
    return {};
}

std::string summarizeValue(const ActionValue& value) {
    struct Writer {
        std::string operator()(const NoneValue&) const { return "-"; }
        std::string operator()(const DnsValue& v) const {
            if (v.automatic) return "авто (провайдер)";
            std::ostringstream out;
            for (std::size_t i = 0; i < v.servers.size(); ++i) {
                if (i) out << ", ";
                out << v.servers[i];
            }
            return out.str();
        }
        std::string operator()(const MtuValue& v) const {
            return std::to_string(v.bytes) + " байт";
        }
        std::string operator()(const TcpValue& v) const {
            std::ostringstream out;
            for (std::size_t i = 0; i < v.settings.size(); ++i) {
                if (i) out << "; ";
                out << "InitialRtt=" << v.settings[i].value;
            }
            return out.str();
        }
        std::string operator()(const PowerPlanValue& v) const { return v.identifier; }
        std::string operator()(const GameDvrValue& v) const {
            return "GameDVR=" + std::to_string(v.game_dvr_enabled.value) +
                   ", AppCapture=" + std::to_string(v.app_capture_enabled.value);
        }
        std::string operator()(const FullscreenValue& v) const {
            return v.existed ? ("флаги: " + v.compatibility_flags) : "не задано";
        }
        std::string operator()(const PriorityValue& v) const {
            switch (v.level) {
                case PriorityLevel::Normal: return "Normal";
                case PriorityLevel::AboveNormal: return "AboveNormal";
                case PriorityLevel::High: return "High";
            }
            return "?";
        }
        std::string operator()(const Cs2MaxPingValue& v) const {
            return "max ping = " + std::to_string(v.max_ping);
        }
    };
    return std::visit(Writer{}, value);
}

Result<std::vector<ActionView>> RemediationService::observeAll() const {
    auto actions = createActions(api_);
    std::vector<FixAction*> raw;
    raw.reserve(actions.size());
    for (auto& a : actions) raw.push_back(a.get());

    auto targets = resolver_(raw);
    std::vector<ActionView> views;

    for (std::size_t i = 0; i < raw.size(); ++i) {
        ActionView view;
        view.id = raw[i]->id();
        view.display_name = actionDisplayName(raw[i]->id());
        view.description = actionDescription(raw[i]->id());

        if (i >= targets.size() || !targets[i]) {
            view.status = ActionStatus::Unsupported;
            view.status_text = "Недоступно";
            view.current_state = "цель недоступна (игра не запущена / нет подходящего интерфейса)";
            views.push_back(std::move(view));
            continue;
        }

        auto observed = raw[i]->observe(*targets[i]);
        if (!observed) {
            view.status = ActionStatus::Unsupported;
            view.status_text = "Недоступно";
            view.current_state = observed.detail().empty() ? "состояние недоступно" : observed.detail();
            views.push_back(std::move(view));
            continue;
        }

        view.status = observed.value().status;
        switch (observed.value().status) {
            case ActionStatus::Recommended: view.status_text = "Рекомендуется"; break;
            case ActionStatus::AlreadyConfigured: view.status_text = "Уже настроено"; break;
            case ActionStatus::Applied: view.status_text = "Применено"; break;
            case ActionStatus::Reverted: view.status_text = "Откатено"; break;
            case ActionStatus::Failed: view.status_text = "Ошибка"; break;
            default: view.status_text = "Не проверено"; break;
        }
        view.current_state = summarizeValue(observed.value().value);
        view.recommended = observed.value().status == ActionStatus::Recommended;
        views.push_back(std::move(view));
    }

    return views;
}

Result<ApplyOutcome> RemediationService::applyIds(const std::vector<ActionId>& ids) {
    auto actions = createActions(api_);
    std::vector<FixAction*> raw;
    for (auto& a : actions) raw.push_back(a.get());

    auto resolved = resolver_(raw);
    std::vector<std::unique_ptr<FixAction>> selected;
    std::vector<ActionTarget> targets;
    const auto wanted = [&ids](ActionId id) {
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    };
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (i < resolved.size() && resolved[i] && wanted(raw[i]->id())) {
            selected.push_back(std::move(actions[i]));
            targets.push_back(*resolved[i]);
        }
    }

    ApplyOutcome outcome;
    if (selected.empty()) {
        outcome.error = RemediationError::InvalidTarget;
        outcome.detail = "нет ни одного действия с доступной целью";
        return outcome;
    }

    FixTransaction transaction(generateTransactionId(), std::move(selected), std::move(targets),
                               backup_store_);

    auto prepared = transaction.prepare();
    if (!prepared) {
        outcome.succeeded = false;
        outcome.error = prepared.code();
        outcome.detail = prepared.detail().empty() ? "prepare failed" : prepared.detail();
        outcome.status = transaction.record().status;
        outcome.outcomes = transaction.record().outcomes;
        return outcome;
    }

    auto executed = transaction.execute();
    outcome.succeeded = executed.ok();
    outcome.error = executed.ok() ? RemediationError::None : executed.code();
    outcome.detail = executed.ok() ? "все действия применены"
                                   : (executed.detail().empty() ? "выполнение прервано" : executed.detail());
    outcome.status = transaction.record().status;
    outcome.outcomes = transaction.record().outcomes;
    return outcome;
}


Result<ApplyOutcome> RemediationService::applyAll() {
    std::vector<ActionId> every;
    for (auto& a : createActions(api_)) every.push_back(a->id());
    return applyIds(every);
}
Result<ApplyOutcome> RemediationService::rollback(const std::string& transaction_id) {
    auto loaded = backup_store_.load(transaction_id);
    if (!loaded) {
        ApplyOutcome outcome;
        outcome.error = loaded.code();
        outcome.detail = loaded.detail().empty() ? "транзакция не найдена" : loaded.detail();
        return outcome;
    }

    TransactionRecord record = loaded.value();
    auto all_actions = createActions(api_);

    // Rebuild the exact action/target set from the persisted record.
    std::vector<std::unique_ptr<FixAction>> selected;
    std::vector<ActionTarget> targets;
    for (const auto& prepared : record.prepared_actions) {
        for (auto& action : all_actions) {
            if (!action) continue;
            if (action->id() == prepared.id) {
                selected.push_back(std::move(action));
                targets.push_back(prepared.target);
                break;
            }
        }
    }

    FixTransaction transaction(record.transaction_id, std::move(selected), std::move(targets),
                               backup_store_);
    transaction.restoreRecord(record);

    auto rolled = transaction.rollback();
    ApplyOutcome outcome;
    outcome.succeeded = rolled.ok();
    outcome.error = rolled.ok() ? RemediationError::None : rolled.code();
    outcome.detail = rolled.ok() ? "изменения откатены"
                                 : (rolled.detail().empty() ? "откат не завершён" : rolled.detail());
    outcome.status = transaction.record().status;
    outcome.outcomes = transaction.record().outcomes;
    return outcome;
}

Result<std::vector<TransactionSummary>> RemediationService::history() const {
    return backup_store_.list();
}

} // namespace remediation
} // namespace gno
