#pragma once

#include "ping_monitor.h"
#include "jitter_calculator.h"
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>
#include <fstream>
#include <chrono>
#include <functional>

namespace gno {

struct NetworkSnapshot {
    std::chrono::steady_clock::time_point timestamp;
    double ping_ms = 0.0;
    double jitter_ms = 0.0;
    double packet_loss_percent = 0.0;
    double download_speed_mbps = 0.0;
    double upload_speed_mbps = 0.0;
    uint32_t active_connections = 0;
};

struct SessionStats {
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point end_time;
    double total_ping_ms = 0.0;
    double total_jitter_ms = 0.0;
    double total_packet_loss_percent = 0.0;
    uint32_t total_samples = 0;
    std::vector<NetworkSnapshot> snapshots;
};

class StatsCollector {
public:
    StatsCollector();
    ~StatsCollector();

    void start(const std::string& session_name);
    void stop();
    bool isRecording() const;

    void recordSnapshot(const NetworkSnapshot& snapshot);
    void recordPing(double latency_ms);
    void recordPacketLoss(double loss_percent);
    void recordJitter(double jitter_ms);

    SessionStats getCurrentSession() const;
    std::vector<SessionStats> getPastSessions() const;

    bool saveSession(const std::string& filepath) const;
    void setMaxSnapshots(uint32_t max);
    uint32_t getSnapshotCount() const;

    using SessionCallback = std::function<void(const SessionStats&)>;
    void setSessionCallback(SessionCallback callback);

private:
    void saveToFile(const std::string& filepath, const SessionStats& session) const;

    std::atomic<bool> recording_{false};
    std::string current_session_name_;
    
    mutable std::mutex session_mutex_;
    SessionStats current_session_;
    std::vector<SessionStats> past_sessions_;
    
    uint32_t max_snapshots_ = 10000;
    SessionCallback session_callback_;
};

} // namespace gno
