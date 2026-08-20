#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace gno {

struct FPSBoostConfig {
    bool disable_game_dvr = false;
    bool disable_fullscreen_optimizations = false;
    bool disable_mouse_acceleration = false;
    bool optimize_power_plan = false;
    bool set_high_priority = false;
    bool optimize_virtual_memory = false;
    uint32_t power_plan_mode = 1;
};

class FPSOptimizerPlatform {
public:
    static bool disableGameDVR();
    static bool disableFullscreenOptimizations();
    static bool disableMouseAcceleration();
    static bool optimizePowerPlan(uint32_t mode);
    static bool setProcessPriority(const std::string& process_name, int priority);
    static bool optimizeVirtualMemory();
    static bool applyConfig(const FPSBoostConfig& config);
    static bool revertAll();
};

} // namespace gno
