#pragma once

// OS target discovery for remediation actions. Windows implementation lives in
// the .cpp; non-Windows builds get stubs that report "not found".

#include "remediation/remediation_types.h"

#include <cstdint>
#include <optional>
#include <string>

namespace gno {
namespace remediation {

// First up, non-loopback adapter with a gateway -> InterfaceTarget{guid, if_index}.
std::optional<ActionTarget> discoverPrimaryInterface();

struct DiscoveredGame {
    std::uint32_t pid = 0;
    std::uint64_t creation_time = 0;
    std::string path;
};

// First running process matching any known game profile process name.
std::optional<DiscoveredGame> discoverRunningGameProcess();

} // namespace remediation
} // namespace gno
