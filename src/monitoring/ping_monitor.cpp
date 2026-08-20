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

bool PingMonitor::isSupported() noexcept {
#ifdef PLATFORM_WINDOWS
    return true;
#else
    return false;
#endif
}

void PingMonitor::start(const std::string& target_ip, uint32_t interval_ms) {
    if (!Ipv4Address::parse(target_ip)) return;

    std::thread completed_worker;
    {
        std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
        if (running_ || (monitor_thread_.joinable() &&
                         monitor_thread_.get_id() == std::this_thread::get_id())) {
            return;
        }
        if (monitor_thread_.joinable()) {
            completed_worker = std::move(monitor_thread_);
        }
    }
    if (completed_worker.joinable()) {
        completed_worker.join();
    }

    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    if (running_) {
        return;
    }
    target_ip_ = target_ip;
    interval_ms_ = std::clamp(interval_ms, 10u, 60000u);
    running_ = true;
    monitor_thread_ = std::thread(&PingMonitor::monitorLoop, this);
}

void PingMonitor::stop() {
    running_ = false;
    wait_cv_.notify_all();
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    if (monitor_thread_.joinable() && monitor_thread_.get_id() != std::this_thread::get_id()) {
        monitor_thread_.join();
    }
}

bool PingMonitor::isRunning() const {
    return running_;
}

ICMPResult PingMonitor::ping(const std::string& target_ip, uint32_t timeout_ms) {
    ICMPResult result;
    result.timestamp = std::chrono::steady_clock::now();
    const auto address = Ipv4Address::parse(target_ip);
    if (!address) {
        result.error = DiagnosticError::MalformedResponse;
        return result;
    }
    if (!isSupported()) {
        result.error = DiagnosticError::UnsupportedCapability;
        return result;
    }
    timeout_ms = std::clamp(timeout_ms, 1u, 10000u);

#ifdef PLATFORM_WINDOWS
    HANDLE icmp_handle = IcmpCreateFile();
    if (icmp_handle == INVALID_HANDLE_VALUE) return result;

    const auto& bytes = address->bytes();
    const auto destination = (static_cast<unsigned long>(bytes[0]) << 24U) |
                             (static_cast<unsigned long>(bytes[1]) << 16U) |
                             (static_cast<unsigned long>(bytes[2]) << 8U) |
                             static_cast<unsigned long>(bytes[3]);
    
    char send_data[] = "GNO";
    char recv_buf[1024] = {0};
    
    DWORD reply = IcmpSendEcho(icmp_handle,
                               htonl(destination),
                               send_data, sizeof(send_data),
                               nullptr, recv_buf, sizeof(recv_buf),
                               timeout_ms);
    
    if (reply > 0) {
        PICMP_ECHO_REPLY echo_reply = (PICMP_ECHO_REPLY)recv_buf;
        result.success = true;
        result.latency_ms = echo_reply->RoundTripTime;
        result.bytes = echo_reply->DataSize;
        result.ttl = echo_reply->Options.Ttl;
        result.error = DiagnosticError::None;
    }
    
    IcmpCloseHandle(icmp_handle);
#endif
    
    return result;
}

std::vector<ICMPResult> PingMonitor::pingBatch(const std::string& target_ip, uint32_t count, uint32_t timeout_ms) {
    std::vector<ICMPResult> results;
    count = std::clamp(count, 1u, 100u);
    timeout_ms = std::clamp(timeout_ms, 1u, 10000u);
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
    std::lock_guard<std::mutex> lock(callback_mutex_);
    ping_callback_ = std::move(callback);
}

void PingMonitor::setStatsCallback(StatsCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    stats_callback_ = std::move(callback);
}

void PingMonitor::monitorLoop() {
    while (running_) {
        ICMPResult result = ping(target_ip_);
        PingCallback ping_callback;
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            ping_callback = ping_callback_;
        }
        if (ping_callback) {
            ping_callback(result);
        }
        updateStats(result);

        std::unique_lock<std::mutex> lock(wait_mutex_);
        wait_cv_.wait_for(lock, std::chrono::milliseconds(interval_ms_), [this] { return !running_; });
    }
}

void PingMonitor::updateStats(const ICMPResult& result) {
    StatsCallback callback;
    PingStats snapshot;
    {
        std::scoped_lock lock(stats_mutex_, callback_mutex_);

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
            for (double latency : stats_.latency_history) {
                sum += latency;
                stats_.min_latency_ms = std::min(stats_.min_latency_ms, latency);
                stats_.max_latency_ms = std::max(stats_.max_latency_ms, latency);
            }
            stats_.avg_latency_ms = sum / stats_.latency_history.size();
        } else {
            stats_.packets_lost++;
        }

        stats_.loss_percent = stats_.packets_sent > 0
            ? (1.0 - static_cast<double>(stats_.packets_received) / stats_.packets_sent) * 100.0
            : 0.0;
        snapshot = stats_;
        callback = stats_callback_;
    }
    if (callback) {
        callback(snapshot);
    }
}

} // namespace gno
