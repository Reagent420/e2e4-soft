#pragma once

// Win32 layer for FPS boost utilities (v2.4.0).
// Timer resolution, RAM cleaner, service control, startup programs.

#include <cstdint>
#include <string>
#include <vector>

namespace gno {
namespace fpsboost {

// ---------------------------------------------------------------- Timer Resolution

// Sets the system timer resolution in 100-ns units. 5000 = 0.5 ms.
// Returns the previous resolution. Call releaseTimerResolution() to undo.
bool setTimerResolution(std::uint32_t hundred_ns);
void releaseTimerResolution();
std::uint32_t currentTimerResolution(); // 100-ns units, 0 = unknown

// ---------------------------------------------------------------- RAM Cleaner

struct RamStats {
    std::uint64_t total_physical = 0;
    std::uint64_t avail_physical = 0;
    std::size_t processes_trimmed = 0;
    std::uint64_t bytes_freed_estimate = 0;
};

// Trims working sets of user-accessible processes + purges standby list.
RamStats cleanRam();

// ---------------------------------------------------------------- Services

enum class ServiceAction { Query, Start, Stop, Disable, AutoStart };

struct ServiceInfo {
    std::string name;
    std::string display_name;
    bool running = false;
    int start_type = -1; // -1=unknown, 0=boot, 1=system, 2=auto, 3=demand, 4=disabled
};

bool serviceControl(const std::string& name, ServiceAction action);
ServiceInfo queryService(const std::string& name);

// ---------------------------------------------------------------- Startup Programs

struct StartupEntry {
    std::string name;
    std::string command;
    std::string location; // registry path
    bool enabled = true;
};

std::vector<StartupEntry> enumStartupPrograms();
bool setStartupEnabled(const std::string& location, const std::string& name, bool enable);

} // namespace fpsboost
} // namespace gno
