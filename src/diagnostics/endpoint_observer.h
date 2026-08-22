#pragma once

#include "diagnostic_types.h"

#include <chrono>
#include <vector>

namespace gno {

class IEndpointObserver {
public:
    virtual ~IEndpointObserver() = default;

    virtual DiagnosticResult<std::vector<ObservedEndpoint>> observe(
        uint32_t pid, std::chrono::milliseconds window,
        const CancellationToken& cancellation) = 0;
};

} // namespace gno
