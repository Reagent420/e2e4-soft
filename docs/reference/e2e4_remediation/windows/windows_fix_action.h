#pragma once

#include "remediation/fix_action.h"
#include "remediation/windows/windows_state_api.h"

#include <memory>
#include <vector>

namespace gno {

std::unique_ptr<FixAction> makeDnsAction(
    std::shared_ptr<WindowsStateApi>,
    DnsValue proposed = {});
std::unique_ptr<FixAction> makeMtuAction(
    std::shared_ptr<WindowsStateApi>,
    MtuValue proposed = {1500});
std::unique_ptr<FixAction> makeTcpParametersAction(
    std::shared_ptr<WindowsStateApi>,
    TcpValue proposed = {});
std::unique_ptr<FixAction> makePowerPlanAction(
    std::shared_ptr<WindowsStateApi>,
    PowerPlanValue proposed = {"8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c"});
std::unique_ptr<FixAction> makeGameDvrAction(
    std::shared_ptr<WindowsStateApi>,
    GameDvrValue proposed = {{true, uint32_t{0}}, {true, uint32_t{0}}});
std::unique_ptr<FixAction> makeFullscreenOptimizationsAction(
    std::shared_ptr<WindowsStateApi>,
    FullscreenValue proposed = {true, "~ DISABLEDXMAXIMIZEDWINDOWEDMODE"});
std::unique_ptr<FixAction> makePriorityAction(
    std::shared_ptr<WindowsStateApi>,
    PriorityValue proposed = PriorityValue::AboveNormal);

std::vector<std::unique_ptr<FixAction>> createWindowsFixActions(
    std::shared_ptr<WindowsStateApi>);

} // namespace gno
