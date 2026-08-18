#include "packet_loss_monitor.h"
#include <thread>
#include <chrono>
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

PacketLossMonitor::PacketLossMonitor() = default;

PacketLossMonitor::~PacketLossMonitor() {
    stop();
}

void PacketLossMonitor::start(const std::string& target_ip, uint32_t interval_ms, uint32_t packet_count) {
    if (running_) return;
    
    target_ip_ = target_ip;
    running_ = true;
    
    monitor_thread_ = std::thread(&PacketLossMonitor::monitorLoop, this);
}

void PacketLossMonitor::stop() {
    running_ = false;
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
}

bool PacketLossMonitor::isRunning() const {
    return running_;
}

PacketLossResult PacketLossMonitor::measure(const std::string& target_ip, uint32_t count, uint32_t timeout_ms) {
    PacketLossResult result;
    result.target_ip = target_ip;
    
#ifdef PLATFORM_WINDOWS
    HANDLE icmp_handle = IcmpCreateFile();
    if (icmp_handle == INVALID_HANDLE_VALUE) return result;
    
    struct in_addr dest_addr;
    if (inet_pton(AF_INET, target_ip.c_str(), &dest_addr) != 1) {
        IcmpCloseHandle(icmp_handle);
        return result;
    }
    
    for (uint32_t i = 0; i < count; i++) {
        char send_data[] = "GNO";
        char recv_buf[1024] = {0};
        
        DWORD reply = IcmpSendEcho(icmp_handle,
                                   dest_addr.S_un.S_addr,
                                   send_data, sizeof(send_data),
                                   nullptr, recv_buf, sizeof(recv_buf),
                                   timeout_ms);
        
        result.packets_sent++;
        
        if (reply > 0) {
            result.packets_received++;
        } else {
            result.packets_lost++;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    IcmpCloseHandle(icmp_handle);
#endif
    
    result.loss_percent = (result.packets_sent > 0) ?
        (static_cast<double>(result.packets_lost) / result.packets_sent) * 100.0 : 0.0;
    
    return result;
}

PacketLossResult PacketLossMonitor::getStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void PacketLossMonitor::resetStats() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_ = PacketLossResult{};
    loss_history_.clear();
}

void PacketLossMonitor::setPacketLossCallback(PacketLossCallback callback) {
    packet_loss_callback_ = std::move(callback);
}

void PacketLossMonitor::monitorLoop() {
    while (running_) {
        PacketLossResult result = measure(target_ip_, 50, 3000);
        
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_ = result;
            loss_history_.push_back(result.loss_percent);
            
            if (loss_history_.size() > 100) {
                loss_history_.erase(loss_history_.begin());
            }
            
            double sum = 0.0;
            for (double loss : loss_history_) {
                sum += loss;
            }
            result.avg_loss_percent = sum / loss_history_.size();
        }
        
        if (packet_loss_callback_) {
            packet_loss_callback_(result);
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

} // namespace gno
