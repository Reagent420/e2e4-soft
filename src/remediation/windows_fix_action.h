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
    WindowsFixAction(ActionId id, WindowsStateApi& api);

    ActionId id() const override { return id_; }
    Result<ActionState> observe(const ActionTarget& target) override;
    Result<PreparedAction> prepare(const ActionTarget& target, const ActionState& current) override;
    Result<ActionState> apply(const PreparedAction& prepared) override;
    Result<ActionState> rollback(const PreparedAction& prepared) override;

private:
    Result<ActionState> transition(const PreparedAction& prepared, bool restoring);

    ActionId id_;
    WindowsStateApi& api_;
};

// Full allowlist, in stable transaction order (7 actions).
std::vector<std::unique_ptr<FixAction>> createActions(WindowsStateApi& api);

} // namespace remediation
} // namespace gno
