#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <thread>
#include <atomic>
#include <mutex>

#include "icmp_probe.h"

namespace gno {

struct ServerNode {
    std::string name;
    std::string city;
    std::string country;
    std::string ip;
    double latitude = 0.0;
    double longitude = 0.0;
};

struct PingResult {
    std::string server_name;
    std::string server_ip;
    double latency_ms = 0.0;
    bool success = false;
};

class SpeedTest {
public:
    SpeedTest();
    ~SpeedTest();

    std::vector<ServerNode> getServers() const;
    void runBenchmark(const std::string& target_ip = "");
    void stop();
    bool isRunning() const;

    PingResult benchmarkServer(const std::string& server_ip, int probes = 5);

    std::vector<PingResult> getResults() const;
    PingResult getBestServer() const;

    // Подмена ICMP-зонда для тестов: детерминированный пинг без сети.
    // См. MultipathEngine::setPathProbe. Устанавливать до runBenchmark,
    // пока рабочий поток ещё не запущен.
    void setPingProbe(IcmpPing probe);

private:
    void benchmarkThread();

    std::vector<ServerNode> servers_;
    std::vector<PingResult> results_;
    mutable std::mutex results_mutex_;
    std::atomic<bool> running_{false};
    std::thread bench_thread_;
    IcmpPing ping_probe_;
};

} // namespace gno
