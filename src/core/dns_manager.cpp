#include "dns_manager.h"
#include <chrono>

#ifdef PLATFORM_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#endif

namespace gno {

DNSManager::DNSManager(Probe probe) : probe_(std::move(probe)) {
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

bool DNSManager::isSupported() noexcept {
#ifdef PLATFORM_WINDOWS
    return true;
#else
    return false;
#endif
}

std::vector<DNSPreset> DNSManager::getPresets() const { return presets_; }

DNSPreset DNSManager::getCurrentDNS() const {
    return presets_.empty() ? DNSPreset{} : presets_[0];
}

DNSBenchmarkResult DNSManager::benchmarkServer(
    const std::string& server_ip,
    const CancellationToken& cancellation) {
    DNSBenchmarkResult result;
    result.server = server_ip;

    const auto address = Ipv4Address::parse(server_ip);
    if (!address) {
        result.error = DiagnosticError::MalformedResponse;
        return result;
    }
    if (cancellation.isCancelled()) {
        result.error = DiagnosticError::Cancelled;
        return result;
    }
    if (!probe_ && !isSupported()) {
        result.error = DiagnosticError::UnsupportedCapability;
        return result;
    }

    constexpr uint32_t timeout_ms = 2000;
    int successful_probes = 0;

    if (probe_) {
        for (int attempt = 0; attempt < 3; ++attempt) {
            if (cancellation.isCancelled()) {
                result.error = DiagnosticError::Cancelled;
                return result;
            }
            if (probe_(*address, timeout_ms)) {
                ++successful_probes;
            }
            if (cancellation.isCancelled()) {
                result.error = DiagnosticError::Cancelled;
                return result;
            }
        }
    } else {

#ifdef PLATFORM_WINDOWS
    HANDLE icmp = IcmpCreateFile();
    if (icmp == INVALID_HANDLE_VALUE) {
        result.error = DiagnosticError::ProbeUnavailable;
        return result;
    }
    {
        struct in_addr addr;
        const auto& bytes = address->bytes();
        const auto destination = (static_cast<unsigned long>(bytes[0]) << 24U) |
                                 (static_cast<unsigned long>(bytes[1]) << 16U) |
                                 (static_cast<unsigned long>(bytes[2]) << 8U) |
                                 static_cast<unsigned long>(bytes[3]);
        addr.S_un.S_addr = htonl(destination);

        char send[] = "GNO";
        char recv_buf[1024] = {};

        double total = 0.0;

        for (int i = 0; i < 3; ++i) {
            if (cancellation.isCancelled()) {
                result.error = DiagnosticError::Cancelled;
                IcmpCloseHandle(icmp);
                return result;
            }
            auto start = std::chrono::steady_clock::now();
            DWORD reply = IcmpSendEcho(icmp, addr.S_un.S_addr,
                send, sizeof(send), nullptr, recv_buf, sizeof(recv_buf), timeout_ms);
            auto end = std::chrono::steady_clock::now();

            if (reply > 0) {
                total += std::chrono::duration<double, std::milli>(end - start).count();
                ++successful_probes;
            }
        }

        IcmpCloseHandle(icmp);
        if (cancellation.isCancelled()) {
            result.error = DiagnosticError::Cancelled;
            return result;
        }
        if (successful_probes > 0) {
            result.latency_ms = total / successful_probes;
        }
    }
#endif
    }

    if (cancellation.isCancelled()) {
        result.error = DiagnosticError::Cancelled;
        return result;
    }
    if (successful_probes > 0) {
        result.success = true;
        result.error = DiagnosticError::None;
    } else {
        result.error = DiagnosticError::Timeout;
    }

    return result;
}

std::vector<DNSBenchmarkResult> DNSManager::benchmarkAll(
    const CancellationToken& cancellation) {
    last_results_.clear();
    for (const auto& p : presets_) {
        if (cancellation.isCancelled()) {
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
