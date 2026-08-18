#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace gno {

class PlatformOptimizer {
public:
    static bool disableGameDVR();
    static bool disableFullscreenOptimizations();
    static bool disableMouseAcceleration();
    static bool optimizePowerPlan(uint32_t mode);
    static bool setProcessPriority(const std::string& process_name, int priority);
    static bool optimizeNetworkStack();
    static bool disableNagleAlgorithm();
    static bool optimizeTCPSettings();
};

} // namespace gno
