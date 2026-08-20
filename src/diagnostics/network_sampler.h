#pragma once

#include "diagnostic_types.h"

namespace gno {

class INetworkSampler {
public:
    virtual ~INetworkSampler() = default;

    virtual MetricSummary sample(const SampleTarget& target, const SamplePlan& plan,
                                 const CancellationToken& cancellation,
                                 DiagnosticError& error) = 0;
};

} // namespace gno
