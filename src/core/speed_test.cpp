#include "speed_test.h"
#include <thread>
#include <chrono>
#include <algorithm>
#include <cmath>

#ifdef PLATFORM_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#endif

namespace gno {

SpeedTest::SpeedTest() {
    servers_ = {
        {"Moscow",      "Moscow",      "Russia",       "8.8.8.8",         55.75, 37.62},
        {"Frankfurt",   "Frankfurt",   "Germany",      "1.1.1.1",         50.11, 8.68},
        {"London",      "London",      "UK",           "195.85.215.1",    51.51, -0.13},
        {"Amsterdam",   "Amsterdam",   "Netherlands",  "93.184.216.34",   52.37, 4.90},
        {"New York",    "New York",    "USA",          "104.16.132.229",  40.71, -74.01},
        {"Los Angeles", "Los Angeles", "USA",          "104.16.133.229",  34.05, -118.24},
        {"Tokyo",       "Tokyo",       "Japan",        "103.2.131.17",    35.68, 139.69},
        {"Singapore",   "Singapore",   "Singapore",    "13.228.10.31",    1.35, 103.82},
        {"Sydney",      "Sydney",      "Australia",    "1.1.1.1",         -33.87, 151.21},
        {"São Paulo",   "São Paulo",   "Brazil",       "208.67.222.222", -23.55, -46.63},
    };
}

SpeedTest::~SpeedTest() { stop(); }

bool SpeedTest::isSupported() noexcept {
#ifdef PLATFORM_WINDOWS
    return true;
#else
    return false;
#endif
}

bool SpeedTest::isRunning() const { return running_; }

std::vector<ServerNode> SpeedTest::getServers() const { return servers_; }

PingResult SpeedTest::benchmarkServer(const std::string& server_ip) {
    PingResult result;
    result.server_name = "Custom";
    result.server_ip = server_ip;
    const auto address = Ipv4Address::parse(server_ip);
    if (!address) {
        result.error = DiagnosticError::MalformedResponse;
        return result;
    }
    if (!isSupported()) {
        result.error = DiagnosticError::UnsupportedCapability;
        return result;
    }

#ifdef PLATFORM_WINDOWS
    HANDLE icmp = IcmpCreateFile();
    if (icmp != INVALID_HANDLE_VALUE) {
        struct in_addr addr;
        const auto& bytes = address->bytes();
        const auto destination = (static_cast<unsigned long>(bytes[0]) << 24U) |
                                 (static_cast<unsigned long>(bytes[1]) << 16U) |
                                 (static_cast<unsigned long>(bytes[2]) << 8U) |
                                 static_cast<unsigned long>(bytes[3]);
        addr.S_un.S_addr = htonl(destination);

        char send[] = "GNO";
        char recv_buf[1024] = {};

        double total = 0;
        int ok = 0;

        for (int i = 0; i < 5; ++i) {
            auto start = std::chrono::steady_clock::now();
            DWORD reply = IcmpSendEcho(icmp, addr.S_un.S_addr,
                send, sizeof(send), nullptr, recv_buf, sizeof(recv_buf), 2000);
            auto end = std::chrono::steady_clock::now();

            if (reply > 0) {
                total += std::chrono::duration<double, std::milli>(end - start).count();
                ok++;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        if (ok > 0) {
            result.latency_ms = total / ok;
            result.success = true;
            result.error = DiagnosticError::None;
        }
        IcmpCloseHandle(icmp);
    }
#endif

    return result;
}

void SpeedTest::runBenchmark(const std::string& target_ip) {
    std::lock_guard<std::mutex> worker_lock(worker_mutex_);
    if (running_) {
        return;
    }
    if (bench_thread_.joinable()) {
        bench_thread_.join();
    }
    {
        std::lock_guard<std::mutex> results_lock(results_mutex_);
        results_.clear();
    }
    if (!isSupported()) {
        return;
    }
    running_ = true;

    try {
        if (!target_ip.empty()) {
            bench_thread_ = std::thread([this, target_ip]() {
                PingResult r;
                r.server_name = "Custom";
                r.server_ip = target_ip;
                const auto address = Ipv4Address::parse(target_ip);
                if (!address) {
                    r.error = DiagnosticError::MalformedResponse;
                }

#ifdef PLATFORM_WINDOWS
                HANDLE icmp = address ? IcmpCreateFile() : INVALID_HANDLE_VALUE;
                if (icmp != INVALID_HANDLE_VALUE) {
                    struct in_addr addr;
                    const auto& bytes = address->bytes();
                    const auto destination = (static_cast<unsigned long>(bytes[0]) << 24U) |
                                             (static_cast<unsigned long>(bytes[1]) << 16U) |
                                             (static_cast<unsigned long>(bytes[2]) << 8U) |
                                             static_cast<unsigned long>(bytes[3]);
                    addr.S_un.S_addr = htonl(destination);

                    char send[] = "GNO";
                    char recv_buf[1024] = {};

                    auto start = std::chrono::steady_clock::now();
                    DWORD reply = IcmpSendEcho(icmp, addr.S_un.S_addr,
                        send, sizeof(send), nullptr, recv_buf, sizeof(recv_buf), 3000);
                    auto end = std::chrono::steady_clock::now();

                    if (reply > 0) {
                        r.latency_ms = std::chrono::duration<double, std::milli>(end - start).count();
                        r.success = true;
                        r.error = DiagnosticError::None;
                    }
                    IcmpCloseHandle(icmp);
                }
#endif

                {
                    std::lock_guard<std::mutex> lock(results_mutex_);
                    results_.push_back(r);
                }
                running_ = false;
            });
        } else {
            bench_thread_ = std::thread(&SpeedTest::benchmarkThread, this);
        }
    } catch (...) {
        running_ = false;
        throw;
    }
}

void SpeedTest::stop() {
    std::lock_guard<std::mutex> worker_lock(worker_mutex_);
    running_ = false;
    if (bench_thread_.joinable()) bench_thread_.join();
}

void SpeedTest::benchmarkThread() {
    for (const auto& server : servers_) {
        if (!running_) break;

        PingResult result;
        result.server_name = server.name;
        result.server_ip = server.ip;
        const auto address = Ipv4Address::parse(server.ip);
        if (!address) {
            result.error = DiagnosticError::MalformedResponse;
        }

#ifdef PLATFORM_WINDOWS
        HANDLE icmp = address ? IcmpCreateFile() : INVALID_HANDLE_VALUE;
        if (icmp != INVALID_HANDLE_VALUE) {
            struct in_addr addr;
            const auto& bytes = address->bytes();
            const auto destination = (static_cast<unsigned long>(bytes[0]) << 24U) |
                                     (static_cast<unsigned long>(bytes[1]) << 16U) |
                                     (static_cast<unsigned long>(bytes[2]) << 8U) |
                                     static_cast<unsigned long>(bytes[3]);
            addr.S_un.S_addr = htonl(destination);

            char send[] = "GNO";
            char recv_buf[1024] = {};

            double total = 0;
            int success_count = 0;

            for (int i = 0; i < 5 && running_; ++i) {
                auto start = std::chrono::steady_clock::now();
                DWORD reply = IcmpSendEcho(icmp, addr.S_un.S_addr,
                    send, sizeof(send), nullptr, recv_buf, sizeof(recv_buf), 2000);
                auto end = std::chrono::steady_clock::now();

                if (reply > 0) {
                    double ms = std::chrono::duration<double, std::milli>(end - start).count();
                    total += ms;
                    success_count++;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }

            if (success_count > 0) {
                result.latency_ms = total / success_count;
                result.success = true;
                result.error = DiagnosticError::None;
            }
            IcmpCloseHandle(icmp);
        }
#else
        result.error = DiagnosticError::UnsupportedCapability;
#endif

        {
            std::lock_guard<std::mutex> lock(results_mutex_);
            results_.push_back(result);
        }
    }
    running_ = false;
}

std::vector<PingResult> SpeedTest::getResults() const {
    std::lock_guard<std::mutex> lock(results_mutex_);
    return results_;
}

PingResult SpeedTest::getBestServer() const {
    std::lock_guard<std::mutex> lock(results_mutex_);
    PingResult best;
    best.latency_ms = 999999.0;
    for (const auto& r : results_) {
        if (r.success && r.latency_ms < best.latency_ms) {
            best = r;
        }
    }
    return best;
}

} // namespace gno
