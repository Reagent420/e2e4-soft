#include "ping_monitor.h"
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

PingMonitor::PingMonitor() = default;
PingMonitor::~PingMonitor() {
    stop();
}

void PingMonitor::start(const std::string& target_ip, uint32_t interval_ms) {
    if (running_) return;
    
    target_ip_ = target_ip;
    running_ = true;
    
    monitor_thread_ = std::thread(&PingMonitor::monitorLoop, this);
}

void PingMonitor::stop() {
    running_ = false;
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
}

bool PingMonitor::isRunning() const {
    return running_;
}

ICMPResult PingMonitor::ping(const std::string& target_ip, uint32_t timeout_ms) {
    ICMPResult result;
    result.timestamp = std::chrono::steady_clock::now();
    
#ifdef PLATFORM_WINDOWS
    HANDLE icmp_handle = IcmpCreateFile();
    if (icmp_handle == INVALID_HANDLE_VALUE) return result;
    
    struct in_addr dest_addr;
    if (inet_pton(AF_INET, target_ip.c_str(), &dest_addr) != 1) {
        IcmpCloseHandle(icmp_handle);
        return result;
    }
    
    char send_data[] = "GNO";
    char recv_buf[1024] = {0};
    
    DWORD reply = IcmpSendEcho(icmp_handle,
                               dest_addr.S_un.S_addr,
                               send_data, sizeof(send_data),
                               nullptr, recv_buf, sizeof(recv_buf),
                               timeout_ms);
    
    if (reply > 0) {
        PICMP_ECHO_REPLY echo_reply = (PICMP_ECHO_REPLY)recv_buf;
        result.success = true;
        result.latency_ms = echo_reply->RoundTripTime;
        result.bytes = echo_reply->DataSize;
        result.ttl = echo_reply->Options.Ttl;
    }
    
    IcmpCloseHandle(icmp_handle);
#endif
    
    return result;
}

std::vector<ICMPResult> PingMonitor::pingBatch(const std::string& target_ip, uint32_t count, uint32_t timeout_ms) {
    std::vector<ICMPResult> results;
    results.reserve(count);
    
    for (uint32_t i = 0; i < count; i++) {
        results.push_back(ping(target_ip, timeout_ms));
        if (i < count - 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    return results;
}

PingStats PingMonitor::getStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void PingMonitor::resetStats() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_ = PingStats{};
}

void PingMonitor::setPingCallback(PingCallback callback) {
    ping_callback_ = std::move(callback);
}

void PingMonitor::setStatsCallback(StatsCallback callback) {
    stats_callback_ = std::move(callback);
}

void PingMonitor::monitorLoop() {
    while (running_) {
        ICMPResult result = ping(target_ip_);
        
        if (ping_callback_) {
            ping_callback_(result);
        }
        
        updateStats(result);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

void PingMonitor::updateStats(const ICMPResult& result) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    stats_.target_ip = target_ip_;
    stats_.packets_sent++;
    stats_.current_latency_ms = result.latency_ms;
    
    if (result.success) {
        stats_.packets_received++;
        stats_.latency_history.push_back(result.latency_ms);
        
        if (stats_.latency_history.size() > 100) {
            stats_.latency_history.erase(stats_.latency_history.begin());
        }
        
        double sum = 0.0;
        stats_.min_latency_ms = 999999.0;
        stats_.max_latency_ms = 0.0;
        
        for (double lat : stats_.latency_history) {
            sum += lat;
            stats_.min_latency_ms = std::min(stats_.min_latency_ms, lat);
            stats_.max_latency_ms = std::max(stats_.max_latency_ms, lat);
        }
        
        stats_.avg_latency_ms = sum / stats_.latency_history.size();
    } else {
        stats_.packets_lost++;
    }
    
    stats_.loss_percent = (stats_.packets_sent > 0) ?
        (1.0 - static_cast<double>(stats_.packets_received) / stats_.packets_sent) * 100.0 : 0.0;
    
    if (stats_callback_) {
        stats_callback_(stats_);
    }
}

} // namespace gno
