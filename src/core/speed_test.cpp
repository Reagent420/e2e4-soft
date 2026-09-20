#include "speed_test.h"
#include <thread>
#include <chrono>
#include <algorithm>
#include <cmath>

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

bool SpeedTest::isRunning() const { return running_; }

std::vector<ServerNode> SpeedTest::getServers() const { return servers_; }

void SpeedTest::setPingProbe(IcmpPing probe) {
    ping_probe_ = std::move(probe);
}

PingResult SpeedTest::benchmarkServer(const std::string& server_ip, int probes) {
    PingResult result;
    result.server_name = "Custom";
    result.server_ip = server_ip;

    const IcmpPing probe = ping_probe_ ? ping_probe_ : defaultIcmpPing();

    double total = 0;
    int ok = 0;

    for (int i = 0; i < probes && i < 10; ++i) {
        const double ms = probe(server_ip, 2000);
        if (ms >= 0.0) {
            total += ms;
            ok++;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    if (ok > 0) {
        result.latency_ms = total / ok;
        result.success = true;
    }

    return result;
}

void SpeedTest::runBenchmark(const std::string& target_ip) {
    if (running_) return;
    results_.clear();
    running_ = true;

    if (!target_ip.empty()) {
        // Зонд захватываем в локалку ДО запуска потока: потом его могут
        // переустановить, а читать ping_probe_ из воркера в тот же момент
        // небезопасно.
        const IcmpPing probe = ping_probe_ ? ping_probe_ : defaultIcmpPing();
        bench_thread_ = std::thread([this, target_ip, probe]() {
            PingResult r;
            r.server_name = "Custom";
            r.server_ip = target_ip;

            const double ms = probe(target_ip, 3000);
            if (ms >= 0.0) {
                r.latency_ms = ms;
                r.success = true;
            }

            {
                std::lock_guard<std::mutex> lock(results_mutex_);
                results_.push_back(r);
            }
            running_ = false;
        });
    } else {
        bench_thread_ = std::thread(&SpeedTest::benchmarkThread, this);
    }
}

void SpeedTest::stop() {
    running_ = false;
    if (bench_thread_.joinable()) bench_thread_.join();
}

void SpeedTest::benchmarkThread() {
    const IcmpPing probe = ping_probe_ ? ping_probe_ : defaultIcmpPing();

    for (const auto& server : servers_) {
        if (!running_) break;

        PingResult result;
        result.server_name = server.name;
        result.server_ip = server.ip;

        double total = 0;
        int success_count = 0;

        for (int i = 0; i < 5 && running_; ++i) {
            const double ms = probe(server.ip, 2000);
            if (ms >= 0.0) {
                total += ms;
                success_count++;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        if (success_count > 0) {
            result.latency_ms = total / success_count;
            result.success = true;
        }

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
