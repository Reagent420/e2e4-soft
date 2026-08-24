#pragma once
// macOS platform layer — implementation pending.
// See MACOS_PORT.md for the full roadmap.

namespace gno {
namespace platform {

// These will provide macOS equivalents for:
// - DNS configuration (networksetup)
// - Service control (launchctl)
// - Power management (pmset)
// - Timer resolution (dispatch_source)
// - Process priority (nice / task_policy)

} // namespace platform
} // namespace gno