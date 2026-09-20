#include "diagnostics/probe_client.h"

#include "core/platform_netscan.h"

#include <algorithm>
#include <chrono>
#include <thread>
#include <utility>
#include <vector>

namespace gno {

namespace {

bool icmpProbeWrapper(const std::string& ip, std::uint32_t timeout_ms,
                      double& latency_ms) {
    const auto result = netscan::icmpDfProbe(ip, 32, static_cast<int>(timeout_ms));
    if (!result.ok) return false;
    latency_ms = result.latency_ms;
    return true;
}

} // namespace

std::vector<ProbeClient::ReferenceEndpoint> ProbeClient::defaultReferences() {
    // Публичные anycast-адреса: отвечают ближайшим узлом, поэтому задержка до
    // них — это «качество выхода в интернет вообще», а не конкретного маршрута.
    return {
        {"Cloudflare 1.1.1.1", "1.1.1.1"},
        {"Google 8.8.8.8", "8.8.8.8"},
    };
}

ProbeClient::ProbeClient(Probe probe, std::vector<ReferenceEndpoint> references)
    : probe_(std::move(probe)), references_(std::move(references)) {
    if (references_.empty()) references_ = defaultReferences();
}

DiagnosticResult<ProbeMeasurement> ProbeClient::measure(
    const ProbeRequest& request, const CancellationToken& cancellation) {
    DiagnosticResult<ProbeMeasurement> result;

    const Probe probe = probe_ ? probe_ : icmpProbeWrapper;

    // Несколько попыток на эталон, потом берём лучший.
    const int attempts = std::max(1, static_cast<int>(request.duration_seconds) / 2);
    const std::uint32_t timeout_ms = 1500;

    double best_latency = -1.0;
    std::string best_region;

    for (const auto& reference : references_) {
        if (cancellation.isCancelled()) {
            result.error = DiagnosticError::Cancelled;
            return result;
        }

        double best_for_reference = -1.0;
        for (int i = 0; i < attempts; ++i) {
            if (cancellation.isCancelled()) {
                result.error = DiagnosticError::Cancelled;
                return result;
            }
            double latency_ms = 0.0;
            if (probe(reference.ip, timeout_ms, latency_ms)) {
                if (best_for_reference < 0.0 || latency_ms < best_for_reference) {
                    best_for_reference = latency_ms;
                }
            }
            if (i + 1 < attempts) {
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
            }
        }

        if (best_for_reference >= 0.0 && (best_latency < 0.0 || best_for_reference < best_latency)) {
            best_latency = best_for_reference;
            best_region = reference.region;
        }
    }

    if (best_latency < 0.0) {
        // Ни один эталон не ответил — скорее всего, ICMP заблокирован или
        // нет связи. Это не катастрофа: сервис сообщит, что сравнивать не с чем.
        result.error = DiagnosticError::ProbeUnavailable;
        return result;
    }

    ProbeMeasurement measurement;
    measurement.probe_region = std::move(best_region);
    measurement.client_to_probe.sent = attempts;
    measurement.client_to_probe.received = attempts; // отвечает — значит получили
    measurement.client_to_probe.median_ms = best_latency;
    measurement.client_to_probe.p95_ms = best_latency;
    // probe_to_game намеренно нулевой: без relay-инфраструктуры (которую GNO
    // сознательно не строит) это измерить невозможно.
    result.value = std::move(measurement);
    result.error = DiagnosticError::None;
    return result;
}

} // namespace gno
