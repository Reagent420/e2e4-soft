#include "diagnostics/endpoint_observer.h"

#include "core/platform_netscan.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace gno {

namespace {

// Разбирает строку вида "93.184.216.34:27015" на IP и порт.
// netscan::remoteTcpEndpointsForPid отдаёт именно такой формат.
std::optional<std::pair<std::string, std::uint16_t>> splitEndpoint(const std::string& text) {
    const auto colon = text.rfind(':');
    if (colon == std::string::npos || colon + 1 >= text.size()) return std::nullopt;

    const std::string ip = text.substr(0, colon);
    const std::string port_text = text.substr(colon + 1);

    std::uint32_t port = 0;
    const auto result = std::from_chars(port_text.data(), port_text.data() + port_text.size(), port);
    if (result.ec != std::errc() || port == 0 || port > 65535) return std::nullopt;

    return std::make_pair(ip, static_cast<std::uint16_t>(port));
}

} // namespace

EndpointObserver::EndpointObserver(EndpointProvider provider)
    : provider_(std::move(provider)) {}

DiagnosticResult<std::vector<ObservedEndpoint>> EndpointObserver::observe(
    uint32_t pid, std::chrono::milliseconds /*window*/, const CancellationToken& cancellation) {
    DiagnosticResult<std::vector<ObservedEndpoint>> result;

    if (pid == 0) {
        result.error = DiagnosticError::GameNotDetected;
        return result;
    }

    const std::vector<std::string> raw = provider_ ? provider_(pid)
                                                   : netscan::remoteTcpEndpointsForPid(pid);
    if (cancellation.isCancelled()) {
        result.error = DiagnosticError::Cancelled;
        return result;
    }

    std::vector<ObservedEndpoint> endpoints;
    endpoints.reserve(raw.size());
    for (const auto& text : raw) {
        if (cancellation.isCancelled()) {
            result.error = DiagnosticError::Cancelled;
            return result;
        }
        const auto parsed = splitEndpoint(text);
        if (!parsed) continue;

        const auto address = Ipv4Address::parse(parsed->first);
        if (!address || address->isUnspecified()) continue;

        ObservedEndpoint endpoint;
        endpoint.ip = *address;
        endpoint.port = parsed->second;
        endpoint.protocol = TransportProtocol::Tcp;
        endpoint.owner_pid = pid;
        endpoints.push_back(endpoint);
    }

    if (endpoints.empty()) {
        // Игра не держит ни одного установленного TCP-соединения: либо процесс
        // не та игра, либо соединения еще не открыты.
        result.error = DiagnosticError::EndpointNotObserved;
        return result;
    }

    // Самый «тяжёлый» кандидат по количеству пакетов неизвестен без сниффинга,
    // поэтому отдаём как есть — сервис выберет первый подходящий.
    result.value = std::move(endpoints);
    result.error = DiagnosticError::None;
    return result;
}

} // namespace gno
