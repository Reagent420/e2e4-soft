#pragma once

#include "fps_optimizer.h"

namespace gno {

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
