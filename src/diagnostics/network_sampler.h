#pragma once

#include "diagnostic_types.h"

#include <functional>

namespace gno {

class INetworkSampler {
public:
    virtual ~INetworkSampler() = default;

    virtual DiagnosticResult<MetricSummary> sample(const SampleTarget& target,
                                                    const SamplePlan& plan,
                                                    const CancellationToken& cancellation) = 0;
};

// Реализация: периодически зондирует цель и собирает сводку задержек.
// Функция пробы инжектируется для тестов (по умолчанию — ICMP из platform_netscan).
class NetworkSampler : public INetworkSampler {
public:
    // Исход одиночной пробы. Exhausted нужен только тестам: источник
    // синтетических данных сообщает, что больше отсчётов не будет, и цикл
    // останавливается детерминированно, не отсчитывая duration_seconds.
    enum class ProbeOutcome {
        Replied,   // ответ получен, latency_ms валидна
        NoReply,   // пакет потерян
        Exhausted, // источник исчерпан (только для тестов)
    };

    using Probe = std::function<ProbeOutcome(const Ipv4Address& target, std::uint32_t timeout_ms,
                                             double& latency_ms)>;

    explicit NetworkSampler(Probe probe = {});
    ~NetworkSampler() override = default;

    DiagnosticResult<MetricSummary> sample(const SampleTarget& target,
                                           const SamplePlan& plan,
                                           const CancellationToken& cancellation) override;

private:
    Probe probe_;
};

} // namespace gno
