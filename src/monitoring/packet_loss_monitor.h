#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <chrono>

namespace gno {

struct PacketLossResult {
    std::string target_ip;
    uint32_t packets_sent = 0;
    uint32_t packets_received = 0;
    uint32_t packets_lost = 0;
    double loss_percent = 0.0;
    double avg_loss_percent = 0.0;
};

class PacketLossMonitor {
public:
    PacketLossMonitor();
    ~PacketLossMonitor();

    void start(const std::string& target_ip, uint32_t interval_ms = 5000, uint32_t packet_count = 20);
    void stop();
    bool isRunning() const;

    PacketLossResult measure(const std::string& target_ip, uint32_t count = 50, uint32_t timeout_ms = 3000);
    
    PacketLossResult getStats() const;
    void resetStats();

    using PacketLossCallback = std::function<void(const PacketLossResult&)>;
    void setPacketLossCallback(PacketLossCallback callback);

private:
    void monitorLoop();

    std::string target_ip_;
    std::atomic<bool> running_{false};
    std::thread monitor_thread_;
    
    mutable std::mutex stats_mutex_;
    PacketLossResult stats_;
    std::vector<double> loss_history_;
    
    PacketLossCallback packet_loss_callback_;
};

} // namespace gno
