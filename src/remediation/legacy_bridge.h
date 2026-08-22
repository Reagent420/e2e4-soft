#pragma once

#include "remediation/backup_store.h"
#include "remediation/windows_state_api.h"

#include <string>

namespace gno {
namespace remediation {

// Routes a legacy fix_action id ("power_plan", "game_dvr", "tcp", "dns", "mtu",
// "priority") through the transactional engine: backup -> apply -> verify.
// Returns a human-readable UTF-8 result. Empty string = id not covered here,
// caller should fall back to its legacy implementation.
std::string applySafeFix(const std::string& legacy_id, WindowsStateApi& api, IBackupStore& store);

} // namespace remediation
} // namespace gno
