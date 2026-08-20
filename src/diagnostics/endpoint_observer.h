#pragma once

#include "diagnostic_types.h"

#include <chrono>
#include <vector>

namespace gno {

class IEndpointObserver {
public:
    virtual ~IEndpointObserver() = default;

    virtual std::vector<ObservedEndpoint> observe(
        uint32_t pid, std::chrono::milliseconds window, DiagnosticError& error) = 0;
};

} // namespace gno
