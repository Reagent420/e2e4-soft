#pragma once

#include <string>

namespace gno {

class FPSOptimizerPlatform {
public:
    static bool applyConfig(const FPSBoostConfig& config);
    static bool revertAll();
};

} // namespace gno
