#include "dns_manager.h"
#include "launch_diagnostics.h"

namespace gno {

DNSManager::DNSManager() {
    presets_ = {
        {"Cloudflare", "1.1.1.1", "1.0.0.1", "Fastest DNS by Cloudflare"},
        {"Cloudflare (Family)", "1.1.1.2", "1.0.0.2", "Cloudflare with malware blocking"},
        {"Google", "8.8.8.8", "8.8.4.4", "Google Public DNS"},
        {"Google (Family)", "8.8.8.8", "8.8.4.4", "Google DNS with SafeSearch"},
        {"Quad9", "9.9.9.9", "149.112.112.112", "Quad9 with threat blocking"},
        {"OpenDNS", "208.67.222.222", "208.67.220.220", "OpenDNS Home"},
        {"Yandex", "77.88.8.8", "77.88.8.1", "Yandex DNS"},
    };
}

DNSManager::~DNSManager() = default;

std::vector<DNSPreset> DNSManager::getPresets() const { return presets_; }

DNSPreset DNSManager::getCurrentDNS() const {
    return presets_.empty() ? DNSPreset{} : presets_[0];
}

bool DNSManager::applyDNS(const std::string& primary, const std::string& secondary) {
    // Раньше это была заглушка: параметры игнорировались, метод возвращал true,
    // если хоть один адаптер включён, а UI показывал «DNS установлен» — хотя
    // ничего не применялось. Теперь — едиственный путь: транзакционный движок
    // (backup → apply → verify → rollback), как и у любого другого фикса.
    std::vector<std::string> servers;
    if (!primary.empty()) servers.push_back(primary);
    if (!secondary.empty()) servers.push_back(secondary);

    last_outcome_ = LaunchDiagnostics::applyDnsDetailed(servers);
    return last_outcome_.applied;
}

bool DNSManager::resetToDHCP() {
    // Сброс на DHCP — это тоже применение значения через движок (automatic=true),
    // а не вызов заглушки applyDNS("auto"), который возвращал true
    // от слова «адаптер включён».
    last_outcome_ = LaunchDiagnostics::applyDnsDetailed({}, true);
    return last_outcome_.applied;
}

const LaunchDiagnostics::FixOutcome& DNSManager::lastOutcome() const {
    return last_outcome_;
}

void DNSManager::setPingProbe(IcmpPing probe) {
    ping_probe_ = std::move(probe);
}

DNSBenchmarkResult DNSManager::benchmarkServer(const std::string& server_ip) {
    DNSBenchmarkResult result;
    result.server = server_ip;

    // Копия в локалку: зонд может быть переустановлен во время бенчмарка,
    // а ссылаться на ping_probe_ из нескольких потоков небезопасно.
    const IcmpPing probe = ping_probe_ ? ping_probe_ : defaultIcmpPing();

    double total = 0;
    int ok = 0;
    for (int i = 0; i < 3; ++i) {
        const double ms = probe(server_ip, 2000);
        if (ms >= 0.0) {
            total += ms;
            ok++;
        }
    }

    if (ok > 0) {
        result.latency_ms = total / ok;
        result.success = true;
    }

    return result;
}

std::vector<DNSBenchmarkResult> DNSManager::benchmarkAll() {
    last_results_.clear();
    for (const auto& p : presets_) {
        last_results_.push_back(benchmarkServer(p.primary));
    }
    return last_results_;
}

std::vector<DNSBenchmarkResult> DNSManager::getResults() const {
    return last_results_;
}

DNSBenchmarkResult DNSManager::getFastestServer() const {
    DNSBenchmarkResult best;
    best.latency_ms = 999999.0;
    for (const auto& r : last_results_) {
        if (r.success && r.latency_ms < best.latency_ms) {
            best = r;
        }
    }
    return best;
}

} // namespace gno
