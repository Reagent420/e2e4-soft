#include "packet_loss_monitor.h"
#include <algorithm>
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

PacketLossResult PacketLossMonitor::measure(const std::string& target_ip, uint32_t count, uint32_t timeout_ms) {
    PacketLossResult result;
    result.target_ip = target_ip;
    const auto address = Ipv4Address::parse(target_ip);
    if (!address) {
        result.error = DiagnosticError::MalformedResponse;
        return result;
    }
    count = std::clamp(count, 1u, 100u);
    timeout_ms = std::clamp(timeout_ms, 1u, 10000u);
    
#ifdef PLATFORM_WINDOWS
    HANDLE icmp_handle = IcmpCreateFile();
    if (icmp_handle == INVALID_HANDLE_VALUE) return result;
    
    struct in_addr dest_addr;
    const auto& bytes = address->bytes();
    const auto destination = (static_cast<unsigned long>(bytes[0]) << 24U) |
                             (static_cast<unsigned long>(bytes[1]) << 16U) |
                             (static_cast<unsigned long>(bytes[2]) << 8U) |
                             static_cast<unsigned long>(bytes[3]);
    dest_addr.S_un.S_addr = htonl(destination);
    
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
    result.error = DiagnosticError::None;
#else
    result.error = DiagnosticError::UnsupportedCapability;
#endif
    
    result.loss_percent = (result.packets_sent > 0) ?
        (static_cast<double>(result.packets_lost) / result.packets_sent) * 100.0 : 0.0;
    
    return result;
}

} // namespace gno
