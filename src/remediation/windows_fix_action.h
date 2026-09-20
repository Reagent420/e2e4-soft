#pragma once

#include "remediation/fix_action.h"
#include "remediation/windows_state_api.h"

#include <memory>
#include <vector>

namespace gno {
namespace remediation {

// Concrete allowlisted Windows action (DNS, MTU, TCP, power plan, GameDVR,
// fullscreen optimizations, process priority).
class WindowsFixAction : public FixAction {
public:
    // dns_override: переопределяет целевое значение для ActionId::Dns. По
    // умолчанию движок применяет свою безопасную пару 1.1.1.1/1.0.0.1, а кнопка
    // «Применить» в таблице DNS должна ставить выбранный пользователем сервер.
    // DnsValue{true, {}} означает сброс на DHCP.
    WindowsFixAction(ActionId id, WindowsStateApi& api, DnsValue dns_override = DnsValue{false, {}});
    void setDnsOverride(DnsValue value) { dns_override_ = std::move(value); }

    ActionId id() const override { return id_; }
    Result<ActionState> observe(const ActionTarget& target) override;
    Result<PreparedAction> prepare(const ActionTarget& target, const ActionState& current) override;
    Result<ActionState> apply(const PreparedAction& prepared) override;
    Result<ActionState> rollback(const PreparedAction& prepared) override;

private:
    Result<ActionState> transition(const PreparedAction& prepared, bool restoring);
    ActionValue proposedValue() const;

    ActionId id_;
    WindowsStateApi& api_;
    DnsValue dns_override_{false, {}};
};

// Full allowlist, in stable transaction order (7 actions).
std::vector<std::unique_ptr<FixAction>> createActions(WindowsStateApi& api);

} // namespace remediation
} // namespace gno
