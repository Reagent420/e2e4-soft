#pragma once

#include "diagnostic_types.h"

#include <chrono>
#include <functional>
#include <string>
#include <vector>

namespace gno {

class IEndpointObserver {
public:
    virtual ~IEndpointObserver() = default;

    virtual DiagnosticResult<std::vector<ObservedEndpoint>> observe(
        uint32_t pid, std::chrono::milliseconds window,
        const CancellationToken& cancellation) = 0;
};

// Реализация: перечисляет установленные TCP-соединения игрового процесса.
// Провайдер инжектируется для тестов (по умолчанию — platform_netscan).
class EndpointObserver : public IEndpointObserver {
public:
    // Возвращает строки "ip:port" для процесса.
    using EndpointProvider = std::function<std::vector<std::string>(uint32_t pid)>;

    explicit EndpointObserver(EndpointProvider provider = {});
    ~EndpointObserver() override = default;

    DiagnosticResult<std::vector<ObservedEndpoint>> observe(
        uint32_t pid, std::chrono::milliseconds window,
        const CancellationToken& cancellation) override;

private:
    EndpointProvider provider_;
};

} // namespace gno
