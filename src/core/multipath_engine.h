#pragma once

#include "route_analyzer.h"
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <map>
#include <chrono>

namespace gno {

struct MultipathConfig {
    uint32_t max_paths = 5;
    uint32_t probe_interval_ms = 1000;
    uint32_t switch_threshold_ms = 50;
    double loss_threshold_percent = 5.0;
    bool auto_switch = true;
    bool load_balance = false;
};

struct PathMetrics {
    uint32_t path_id = 0;
    double latency_ms = 0.0;
    double jitter_ms = 0.0;
    double packet_loss_percent = 0.0;
    uint64_t bytes_sent = 0;
    uint64_t bytes_received = 0;
    std::chrono::steady_clock::time_point last_update;
    bool is_healthy = true;
};

class MultipathEngine {
public:
    explicit MultipathEngine(const MultipathConfig& config = {});
    ~MultipathEngine();

    void start(const std::string& game_server_ip);
    void stop();
    bool isActive() const;

    void setConfig(const MultipathConfig& config);
    MultipathConfig getConfig() const;

    std::vector<PathMetrics> getPathMetrics() const;
    std::optional<PathMetrics> getBestPath() const;
    uint32_t getActivePathId() const;

    void addRoute(const std::string& gateway_ip);
    void removeRoute(const std::string& gateway_ip);

    using PathSwitchCallback = std::function<void(uint32_t old_path, uint32_t new_path)>;
    void setPathSwitchCallback(PathSwitchCallback callback);

    using MetricsUpdateCallback = std::function<void(const std::vector<PathMetrics>&)>;
    void setMetricsUpdateCallback(MetricsUpdateCallback callback);

private:
    void probeLoop();
    void evaluatePathSwitch();
    void switchToPath(uint32_t path_id);
    double calculatePathScore(const PathMetrics& metrics) const;

    MultipathConfig config_;
    std::string game_server_ip_;
    std::atomic<bool> active_{false};
    std::atomic<uint32_t> active_path_id_{0};
    
    mutable std::mutex paths_mutex_;
    std::map<uint32_t, PathMetrics> path_metrics_;
    std::map<uint32_t, std::string> path_gateways_;
    uint32_t next_path_id_{0};
    
    std::thread probe_thread_;
    PathSwitchCallback path_switch_callback_;
    MetricsUpdateCallback metrics_update_callback_;
    
    RouteAnalyzer route_analyzer_;
};

} // namespace gno
