#include "fps_optimizer_linux.h"

namespace gno {

bool FPSOptimizerPlatform::applyConfig(const FPSBoostConfig& config) {
    return false;
}

bool FPSOptimizerPlatform::revertAll() {
    return true;
}

} // namespace gno
