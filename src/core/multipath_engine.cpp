#include "multipath_engine.h"
#include <algorithm>
#include <cmath>

namespace gno {

MultipathEngine::MultipathEngine(const MultipathConfig& config)
    : config_(config) {}

MultipathEngine::~MultipathEngine() {
    stop();
}

void MultipathEngine::start(const std::string& game_server_ip) {
    if (active_) return;
    
    game_server_ip_ = game_server_ip;
    active_ = true;
    
    auto interfaces = route_analyzer_.getInterfaces();
    for (size_t i = 0; i < interfaces.size() && i < config_.max_paths; i++) {
        addRoute(interfaces[i].gateway);
    }
    
    probe_thread_ = std::thread(&MultipathEngine::probeLoop, this);
}

void MultipathEngine::stop() {
    active_ = false;
    if (probe_thread_.joinable()) {
        probe_thread_.join();
    }
}

bool MultipathEngine::isActive() const {
    return active_;
}

void MultipathEngine::setConfig(const MultipathConfig& config) {
    config_ = config;
}

MultipathConfig MultipathEngine::getConfig() const {
    return config_;
}

std::vector<PathMetrics> MultipathEngine::getPathMetrics() const {
    std::lock_guard<std::mutex> lock(paths_mutex_);
    std::vector<PathMetrics> metrics;
    for (const auto& [id, m] : path_metrics_) {
        metrics.push_back(m);
    }
    return metrics;
}

std::optional<PathMetrics> MultipathEngine::getBestPath() const {
    std::lock_guard<std::mutex> lock(paths_mutex_);
    std::optional<PathMetrics> best;
    double best_score = -1.0;
    
    for (const auto& [id, m] : path_metrics_) {
        if (m.is_healthy) {
            double score = calculatePathScore(m);
            if (score > best_score) {
                best_score = score;
                best = m;
            }
        }
    }
    
    return best;
}

uint32_t MultipathEngine::getActivePathId() const {
    return active_path_id_;
}

void MultipathEngine::addRoute(const std::string& gateway_ip) {
    std::lock_guard<std::mutex> lock(paths_mutex_);
    uint32_t path_id = next_path_id_++;
    
    path_gateways_[path_id] = gateway_ip;
    path_metrics_[path_id] = PathMetrics{path_id, 0.0, 0.0, 0.0, 0, 0,
        std::chrono::steady_clock::now(), true};
}

void MultipathEngine::removeRoute(const std::string& gateway_ip) {
    std::lock_guard<std::mutex> lock(paths_mutex_);
    for (auto it = path_gateways_.begin(); it != path_gateways_.end(); ) {
        if (it->second == gateway_ip) {
            path_metrics_.erase(it->first);
            it = path_gateways_.erase(it);
        } else {
            ++it;
        }
    }
}

void MultipathEngine::setPathSwitchCallback(PathSwitchCallback callback) {
    path_switch_callback_ = std::move(callback);
}

void MultipathEngine::setMetricsUpdateCallback(MetricsUpdateCallback callback) {
    metrics_update_callback_ = std::move(callback);
}

void MultipathEngine::probeLoop() {
    while (active_) {
        std::lock_guard<std::mutex> lock(paths_mutex_);
        
        for (auto& [id, metrics] : path_metrics_) {
            std::string gateway = path_gateways_[id];
            
            double latency = route_analyzer_.measureLatency(game_server_ip_, 5);
            double loss = route_analyzer_.measurePacketLoss(game_server_ip_, 20);
            
            double old_latency = metrics.latency_ms;
            metrics.latency_ms = latency;
            metrics.packet_loss_percent = loss;
            
            if (old_latency > 0) {
                metrics.jitter_ms = std::abs(latency - old_latency);
            }
            
            metrics.last_update = std::chrono::steady_clock::now();
            metrics.is_healthy = (loss < config_.loss_threshold_percent);
            
            metrics.path_id = id;
        }
        
        if (metrics_update_callback_) {
            std::vector<PathMetrics> all_metrics;
            for (const auto& [id, m] : path_metrics_) {
                all_metrics.push_back(m);
            }
            metrics_update_callback_(all_metrics);
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.probe_interval_ms));
    }
}

void MultipathEngine::evaluatePathSwitch() {
    if (!config_.auto_switch) return;
    
    auto best = getBestPath();
    if (!best) return;
    
    uint32_t best_id = best->path_id;
    if (best_id != active_path_id_) {
        double current_score = 0.0;
        double best_score = calculatePathScore(*best);
        
        {
            std::lock_guard<std::mutex> lock(paths_mutex_);
            auto it = path_metrics_.find(active_path_id_);
            if (it != path_metrics_.end()) {
                current_score = calculatePathScore(it->second);
            }
        }
        
        if (best_score > current_score + config_.switch_threshold_ms) {
            switchToPath(best_id);
        }
    }
}

void MultipathEngine::switchToPath(uint32_t path_id) {
    uint32_t old_path = active_path_id_;
    active_path_id_ = path_id;
    
    if (path_switch_callback_) {
        path_switch_callback_(old_path, path_id);
    }
}

double MultipathEngine::calculatePathScore(const PathMetrics& metrics) const {
    double score = 100.0;
    
    score -= metrics.latency_ms * 0.5;
    score -= metrics.jitter_ms * 2.0;
    score -= metrics.packet_loss_percent * 10.0;
    
    if (!metrics.is_healthy) {
        score -= 50.0;
    }
    
    auto age = std::chrono::steady_clock::now() - metrics.last_update;
    if (age > std::chrono::seconds(5)) {
        score -= 20.0;
    }
    
    return std::max(0.0, score);
}

} // namespace gno
