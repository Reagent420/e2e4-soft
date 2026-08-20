#pragma once

#include "remediation/fix_action.h"
#include "remediation/windows/windows_state_api.h"

#include <memory>
#include <vector>

namespace gno {

std::vector<std::unique_ptr<FixAction>> createPlatformFixActions(
    std::shared_ptr<WindowsStateApi> api = {});

} // namespace gno
