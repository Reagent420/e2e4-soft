#pragma once

#include "diagnostic_types.h"

namespace gno {

class IProbeClient {
public:
    virtual ~IProbeClient() = default;

    virtual DiagnosticResult<ProbeMeasurement> measure(
        const ProbeRequest& request, const CancellationToken& cancellation) = 0;
};

} // namespace gno
