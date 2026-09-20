#pragma once

#include <string>
#include <vector>

#include "launch_diagnostics.h"
#include "icmp_probe.h"

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

    // Применяет выбранные серверы через транзакционный движок. Возвращает true
    // только если движок реально изменил DNS. Результат последнего вызова
    // доступен через lastOutcome() — сообщение движка показывается пользователю
    // вместо выдуманного «DNS установлен».
    bool applyDNS(const std::string& primary, const std::string& secondary = "");
    bool resetToDHCP();
    const LaunchDiagnostics::FixOutcome& lastOutcome() const;

    std::vector<DNSBenchmarkResult> benchmarkAll();
    std::vector<DNSBenchmarkResult> getResults() const;
    DNSBenchmarkResult benchmarkServer(const std::string& server_ip);
    DNSBenchmarkResult getFastestServer() const;

    // Подмена ICMP-зонда для тестов: позволяет прогнать бенчмарк DNS
    // детерминированно, без реальной сети. Продуктовый код не вызывает —
    // используется defaultIcmpPing(). По тем же причинам, что и
    // MultipathEngine::setPathProbe.
    void setPingProbe(IcmpPing probe);

private:
    std::vector<DNSPreset> presets_;
    std::vector<DNSBenchmarkResult> last_results_;
    LaunchDiagnostics::FixOutcome last_outcome_;
    IcmpPing ping_probe_;
};

} // namespace gno
