#pragma once

#include "diagnostic_types.h"

#include <functional>
#include <string>
#include <vector>

namespace gno {

class IProbeClient {
public:
    virtual ~IProbeClient() = default;

    virtual DiagnosticResult<ProbeMeasurement> measure(
        const ProbeRequest& request, const CancellationToken& cancellation) = 0;
};

// Реализация: измеряет задержку до публичных эталонных адресов (anycast
// Cloudflare/Google). Это позволяет отделить «плохо ходит вообще весь трафик»
// от «плохо ходит именно до серверов этой игры».
//
// ВАЖНО: GNO намеренно не строит relay/VPN-инфраструктуру (см. capability
// matrix), поэтому измерить «probe -> сервер игры» с клиента невозможно.
// Поле probe_to_game остаётся нулевым, а в evidence сервис честно пишет,
// что для такого вывода нужен релей. client_to_probe — реальные данные.
class ProbeClient : public IProbeClient {
public:
    struct ReferenceEndpoint {
        const char* region;
        const char* ip;
    };

    // Зондирует эталонный адрес; true = ответ, latency_ms валидна.
    using Probe = std::function<bool(const std::string& ip, std::uint32_t timeout_ms,
                                     double& latency_ms)>;

    explicit ProbeClient(Probe probe = {}, std::vector<ReferenceEndpoint> references = {});
    ~ProbeClient() override = default;

    DiagnosticResult<ProbeMeasurement> measure(
        const ProbeRequest& request, const CancellationToken& cancellation) override;

    static std::vector<ReferenceEndpoint> defaultReferences();

private:
    Probe probe_;
    std::vector<ReferenceEndpoint> references_;
};

} // namespace gno
