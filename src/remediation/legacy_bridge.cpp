#include "remediation/legacy_bridge.h"
#include "remediation/remediation_service.h"
#include "remediation/fix_transaction.h"
#include "remediation/target_discovery.h"
#include "remediation/windows_fix_action.h"

#include <memory>
#include <vector>

namespace gno {
namespace remediation {

namespace {

struct Mapped {
    ActionId id;
    bool needs_interface = false;
    bool needs_process = false;
};

bool mapLegacy(const std::string& legacy_id, Mapped& out) {
    if (legacy_id == "power_plan") out = {ActionId::PowerPlan};
    else if (legacy_id == "game_dvr") out = {ActionId::GameDvr};
    else if (legacy_id == "tcp") out = {ActionId::TcpParameters};
    else if (legacy_id == "dns") out = {ActionId::Dns, true};
    else if (legacy_id == "mtu") out = {ActionId::Mtu, true};
    else if (legacy_id == "priority") out = {ActionId::ProcessPriority, false, true};
    else return false;
    return true;
}

} // namespace

std::string applySafeFix(const std::string& legacy_id, WindowsStateApi& api, IBackupStore& store) {
    Mapped mapped;
    if (!mapLegacy(legacy_id, mapped))
        return {};

    ActionTarget target{NoTarget{}};
    if (mapped.needs_interface) {
        auto iface = discoverPrimaryInterface();
        if (!iface) return "Не удалось: не найден активный сетевой адаптер с шлюзом.";
        target = *iface;
    } else if (mapped.needs_process) {
        auto game = discoverRunningGameProcess();
        if (!game)
            return "Приоритет применяется автоматически при запуске игры (профиль игры). Запустите игру и повторите.";
        target = ProcessTarget{game->pid, game->creation_time, game->path};
    }

    auto all_actions = createActions(api);
    std::vector<std::unique_ptr<FixAction>> selected;
    for (auto& action : all_actions) {
        if (action->id() == mapped.id) {
            selected.push_back(std::move(action));
            break;
        }
    }
    if (selected.empty())
        return {};

    std::vector<ActionTarget> targets{target};
    FixTransaction transaction("tx-legacy-" + legacy_id + "-" +
                                   std::to_string(
                                       std::chrono::steady_clock::now().time_since_epoch().count()),
                               std::move(selected), std::move(targets), store);

    auto prepared = transaction.prepare();
    if (!prepared)
        return "Не удалось подготовить изменение: " + prepared.detail();

    auto executed = transaction.execute();
    if (!executed)
        return "Применение прервано: " + executed.detail();

    return actionDisplayName(mapped.id) + ": применено (создан бэкап, откат - кнопка «Откатить последнее» во вкладке «Оптимизация Win»).";
}

} // namespace remediation
} // namespace gno
