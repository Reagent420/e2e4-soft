#pragma once

#include "core/launch_diagnostics.h"

#include <string>
#include <vector>

namespace gno {

struct PlainSection {
    std::string title;
    std::vector<std::string> lines;
};

// Builds the "human view": what is wrong, what we can do, what the user
// should do themselves, and what GNO deliberately does not do.
class PlainLanguageReport {
public:
    static std::vector<PlainSection> build(double avg_rtt_ms, double jitter_ms,
                                           double packet_loss_percent, bool network_ok,
                                           const GameDiagnostics* launch, bool elevated);
};

} // namespace gno
