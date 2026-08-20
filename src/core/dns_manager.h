#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "diagnostics/diagnostic_types.h"

namespace gno {

struct DNSPreset {
    std::string name;
    std::string primary;
    std::string secondary;
    std::string description;
};

struct DNSBenchmarkResult {
    std::string server;
    double latency_ms = 0.0;
    bool success = false;
    DiagnosticError error = DiagnosticError::InternalFailure;
};

class DNSManager {
public:
    using Probe = std::function<bool(const Ipv4Address&, uint32_t timeout_ms)>;

    explicit DNSManager(Probe probe = {});
    ~DNSManager();

    static bool isSupported() noexcept;

    std::vector<DNSPreset> getPresets() const;
    DNSPreset getCurrentDNS() const;

    std::vector<DNSBenchmarkResult> benchmarkAll(
        const CancellationToken& cancellation = {});
    std::vector<DNSBenchmarkResult> getResults() const;
    DNSBenchmarkResult benchmarkServer(
        const std::string& server_ip,
        const CancellationToken& cancellation = {});
    DNSBenchmarkResult getFastestServer() const;

private:
    std::vector<DNSPreset> presets_;
    std::vector<DNSBenchmarkResult> last_results_;
    Probe probe_;
};

} // namespace gno
