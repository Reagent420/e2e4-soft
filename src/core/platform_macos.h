#pragma once

#include <string>

namespace gno {

class PlatformOptimizer {
public:
    static bool optimizeNetworkStack();
    static bool setProcessPriority(const std::string& process_name, int priority);
    static bool optimizeSystemSettings();
};

} // namespace gno
