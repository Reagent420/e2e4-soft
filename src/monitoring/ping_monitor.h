#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <chrono>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "diagnostics/diagnostic_types.h"

namespace gno {

struct ICMPResult {
    bool success = false;
    double latency_ms = 0.0;
    uint32_t ttl = 0;
    uint32_t bytes = 0;
    std::chrono::steady_clock::time_point timestamp;
    DiagnosticError error = DiagnosticError::InternalFailure;
};

struct PingStats {
    std::string target_ip;
    uint32_t packets_sent = 0;
    uint32_t packets_received = 0;
    uint32_t packets_lost = 0;
    double loss_percent = 0.0;
    double min_latency_ms = 0.0;
    double max_latency_ms = 0.0;
    double avg_latency_ms = 0.0;
    double current_latency_ms = 0.0;
    std::vector<double> latency_history;
};

class PingMonitor {
public:
    PingMonitor();
    ~PingMonitor();

    static bool isSupported() noexcept;

    void start(const std::string& target_ip, uint32_t interval_ms = 1000);
    void stop();
    bool isRunning() const;

    ICMPResult ping(const std::string& target_ip, uint32_t timeout_ms = 3000);
    std::vector<ICMPResult> pingBatch(const std::string& target_ip, uint32_t count, uint32_t timeout_ms = 3000);

    PingStats getStats() const;
    void resetStats();

    using PingCallback = std::function<void(const ICMPResult&)>;
    void setPingCallback(PingCallback callback);

    using StatsCallback = std::function<void(const PingStats&)>;
    void setStatsCallback(StatsCallback callback);

private:
    void monitorLoop();
    void updateStats(const ICMPResult& result);

    std::string target_ip_;
    std::atomic<bool> running_{false};
    std::thread monitor_thread_;
    std::mutex lifecycle_mutex_;
    uint32_t interval_ms_ = 1000;
    std::mutex wait_mutex_;
    std::condition_variable wait_cv_;
    
    mutable std::mutex stats_mutex_;
    mutable std::mutex callback_mutex_;
    PingStats stats_;
    
    PingCallback ping_callback_;
    StatsCallback stats_callback_;
};

} // namespace gno
