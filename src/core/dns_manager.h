#pragma once

#include <string>
#include <vector>

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
};

class DNSManager {
public:
    DNSManager();
    ~DNSManager();

    std::vector<DNSPreset> getPresets() const;
    DNSPreset getCurrentDNS() const;
    bool applyDNS(const std::string& primary, const std::string& secondary = "");
    bool resetToDHCP();

    std::vector<DNSBenchmarkResult> benchmarkAll();
    std::vector<DNSBenchmarkResult> getResults() const;
    DNSBenchmarkResult benchmarkServer(const std::string& server_ip);
    DNSBenchmarkResult getFastestServer() const;

private:
    std::vector<DNSPreset> presets_;
    std::vector<DNSBenchmarkResult> last_results_;
};

} // namespace gno
