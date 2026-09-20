#include "diagnostics/network_sampler.h"

#include "core/platform_netscan.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>
#include <utility>
#include <vector>

namespace gno {

namespace {

NetworkSampler::ProbeOutcome icmpProbe(const Ipv4Address& target, std::uint32_t timeout_ms,
                                       double& latency_ms) {
    const auto result = netscan::icmpDfProbe(target.toString(), 32, static_cast<int>(timeout_ms));
    if (!result.ok) return NetworkSampler::ProbeOutcome::NoReply;
    latency_ms = result.latency_ms;
    return NetworkSampler::ProbeOutcome::Replied;
}

double medianOf(std::vector<double> sorted) {
    if (sorted.empty()) return 0.0;
    const std::size_t n = sorted.size();
    if (n % 2 == 1) return sorted[n / 2];
    return (sorted[n / 2 - 1] + sorted[n / 2]) / 2.0;
}

// Nearest-rank p95: индекс ceil(0.95*n)-1, ограниченный диапазоном массива.
double p95Of(std::vector<double> sorted) {
    if (sorted.empty()) return 0.0;
    const std::size_t n = sorted.size();
    std::size_t index = static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(n)));
    index = (index == 0) ? 0 : index - 1;
    if (index >= n) index = n - 1;
    return sorted[index];
}

// Средний модуль разности соседних отсчётов — классическая мера джиттера.
double jitterOf(const std::vector<double>& latencies) {
    if (latencies.size() < 2) return 0.0;
    double total = 0.0;
    for (std::size_t i = 1; i < latencies.size(); ++i) {
        total += std::fabs(latencies[i] - latencies[i - 1]);
    }
    return total / static_cast<double>(latencies.size() - 1);
}

} // namespace

NetworkSampler::NetworkSampler(Probe probe) : probe_(std::move(probe)) {}

DiagnosticResult<MetricSummary> NetworkSampler::sample(const SampleTarget& target,
                                                       const SamplePlan& plan,
                                                       const CancellationToken& cancellation) {
    DiagnosticResult<MetricSummary> result;

    if (target.ip.isUnspecified()) {
        // port может быть 0: шлюз зондируется ICMP, портов не использующим.
        result.error = DiagnosticError::UnsupportedCapability;
        return result;
    }

    const Probe probe = probe_ ? probe_ : icmpProbe;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(plan.duration_seconds);

    std::uint32_t sent = 0;
    std::uint32_t received = 0;
    std::vector<double> latencies;
    latencies.reserve(64);

    while (!cancellation.isCancelled()) {
        double latency_ms = 0.0;
        const ProbeOutcome outcome = probe(target.ip, plan.timeout_ms, latency_ms);
        if (outcome == ProbeOutcome::Exhausted) break;
        ++sent;
        if (outcome == ProbeOutcome::Replied) {
            ++received;
            latencies.push_back(latency_ms);
        }

        if (std::chrono::steady_clock::now() >= deadline) break;
        if (cancellation.isCancelled()) break;

        // Между пробами ждём, не занимая CPU. sleep_for прерывается по отмене
        // не мгновенно — но цикл проверяет флаг на следующей итерации.
        if (plan.interval_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(plan.interval_ms));
        }
    }

    if (cancellation.isCancelled()) {
        result.error = DiagnosticError::Cancelled;
        return result;
    }

    MetricSummary summary;
    summary.sent = sent;
    summary.received = received;
    summary.loss_percent = sent > 0 ? (1.0 - static_cast<double>(received) / static_cast<double>(sent)) * 100.0
                                    : 0.0;

    // Меньше трёх ответов — перцентили и джиттер бессмысленны.
    if (received < 3) {
        result.value = summary;
        result.error = DiagnosticError::InsufficientResponses;
        return result;
    }

    // Джиттер — это разброс СОСЕДНИХ отсчётов во времени, поэтому считается по
    // исходной последовательности до какой-либо сортировки.
    summary.jitter_ms = jitterOf(latencies);

    std::sort(latencies.begin(), latencies.end());
    summary.median_ms = medianOf(latencies);
    summary.p95_ms = p95Of(latencies);

    result.value = summary;
    result.error = DiagnosticError::None;
    return result;
}

} // namespace gno
