#include "dns_manager.h"
#include <thread>
#include <chrono>
#include <algorithm>

#ifdef PLATFORM_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#endif

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

DNSBenchmarkResult DNSManager::benchmarkServer(
    const std::string& server_ip,
    const std::atomic<bool>* cancellation) {
    DNSBenchmarkResult result;
    result.server = server_ip;

    if (cancellation && cancellation->load()) {
        return result;
    }

#ifdef PLATFORM_WINDOWS
    HANDLE icmp = IcmpCreateFile();
    if (icmp != INVALID_HANDLE_VALUE) {
        struct in_addr addr;
        inet_pton(AF_INET, server_ip.c_str(), &addr);

        char send[] = "GNO";
        char recv_buf[1024] = {};

        double total = 0;
        int ok = 0;

        for (int i = 0; i < 3; ++i) {
            if (cancellation && cancellation->load()) {
                break;
            }
            auto start = std::chrono::steady_clock::now();
            DWORD reply = IcmpSendEcho(icmp, addr.S_un.S_addr,
                send, sizeof(send), nullptr, recv_buf, sizeof(recv_buf), 2000);
            auto end = std::chrono::steady_clock::now();

            if (reply > 0) {
                total += std::chrono::duration<double, std::milli>(end - start).count();
                ok++;
            }
        }

        if (ok > 0) {
            result.latency_ms = total / ok;
            result.success = true;
        }
        IcmpCloseHandle(icmp);
    }
#endif

    return result;
}

std::vector<DNSBenchmarkResult> DNSManager::benchmarkAll(
    const std::atomic<bool>* cancellation) {
    last_results_.clear();
    for (const auto& p : presets_) {
        if (cancellation && cancellation->load()) {
            break;
        }
        last_results_.push_back(benchmarkServer(p.primary, cancellation));
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
