#include "route_analyzer.h"
#include <algorithm>
#include <sstream>
#include <thread>
#include <chrono>
#include <memory>

#ifdef PLATFORM_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

namespace gno {

struct RouteAnalyzer::Impl {
    bool winsock_initialized = false;
    
    Impl() {
#ifdef PLATFORM_WINDOWS
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0) {
            winsock_initialized = true;
        }
#endif
    }
    
    ~Impl() {
#ifdef PLATFORM_WINDOWS
        if (winsock_initialized) {
            WSACleanup();
        }
#endif
    }
};

RouteAnalyzer::RouteAnalyzer() : impl_(std::make_unique<Impl>()) {}
RouteAnalyzer::~RouteAnalyzer() = default;

std::vector<RouteInfo> RouteAnalyzer::getRoutes() {
    std::vector<RouteInfo> routes;
    
#ifdef PLATFORM_WINDOWS
    PMIB_IPFORWARD_TABLE2 table = nullptr;
    ULONG size = 0;
    
    GetIpForwardTable2(AF_UNSPEC, &table);
    
    if (table) {
        for (ULONG i = 0; i < table->NumEntries; i++) {
            RouteInfo route;
            auto& entry = table->Table[i];
            
            char dest_str[INET6_ADDRSTRLEN] = {0};
            char gateway_str[INET6_ADDRSTRLEN] = {0};
            
            inet_ntop(entry.DestinationPrefix.Prefix.si_family,
                      &entry.DestinationPrefix.Prefix.Ipv4.sin_addr,
                      dest_str, sizeof(dest_str));
            
            inet_ntop(entry.NextHop.si_family,
                      &entry.NextHop.Ipv4.sin_addr,
                      gateway_str, sizeof(gateway_str));
            
            route.destination_ip = dest_str;
            route.gateway_ip = gateway_str;
            route.metric = entry.Metric;
            route.interface_index = entry.InterfaceIndex;
            
            routes.push_back(route);
        }
        FreeMibTable(table);
    }
#endif
    
    return routes;
}

std::vector<NetworkInterface> RouteAnalyzer::getInterfaces() {
    std::vector<NetworkInterface> interfaces;
    
#ifdef PLATFORM_WINDOWS
    PIP_ADAPTER_ADDRESSES adapter_addresses = nullptr;
    ULONG size = 0;
    ULONG result = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, nullptr, nullptr, &size);
    
    if (result == ERROR_BUFFER_OVERFLOW) {
        adapter_addresses = (PIP_ADAPTER_ADDRESSES)malloc(size);
        result = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, nullptr, adapter_addresses, &size);
        
        if (result == NO_ERROR) {
            for (PIP_ADAPTER_ADDRESSES adapter = adapter_addresses; adapter; adapter = adapter->Next) {
                if (adapter->OperStatus == IfOperStatusUp) {
                    NetworkInterface iface;
                    iface.name = adapter->AdapterName;
                    iface.description = "Interface";
                    iface.is_up = true;
                    iface.is_wireless = (adapter->IfType == IF_TYPE_IEEE80211);
                    
                    for (PIP_ADAPTER_UNICAST_ADDRESS unicast = adapter->FirstUnicastAddress;
                         unicast; unicast = unicast->Next) {
                        if (unicast->Address.lpSockaddr->sa_family == AF_INET) {
                            char ip_str[INET_ADDRSTRLEN] = {0};
                            inet_ntop(AF_INET,
                                      &((struct sockaddr_in*)unicast->Address.lpSockaddr)->sin_addr,
                                      ip_str, sizeof(ip_str));
                            iface.ip_address = ip_str;
                            break;
                        }
                    }
                    
                    interfaces.push_back(iface);
                }
            }
        }
        
        free(adapter_addresses);
    }
#endif
    
    return interfaces;
}

std::optional<RouteInfo> RouteAnalyzer::getRouteTo(const std::string& destination) {
    auto routes = getRoutes();
    for (const auto& route : routes) {
        if (route.destination_ip == "0.0.0.0" || route.destination_ip == destination) {
            return route;
        }
    }
    return std::nullopt;
}

std::vector<HopInfo> RouteAnalyzer::traceroute(const std::string& destination, uint32_t max_hops) {
    std::vector<HopInfo> hops;
    
#ifdef PLATFORM_WINDOWS
    HANDLE icmp_handle = IcmpCreateFile();
    if (icmp_handle == INVALID_HANDLE_VALUE) return hops;
    
    struct in_addr dest_addr;
    inet_pton(AF_INET, destination.c_str(), &dest_addr);
    
    for (uint32_t ttl = 1; ttl <= max_hops; ttl++) {
        HopInfo hop;
        hop.hop_number = ttl;
        
        char send_data[] = "GNO";
        char recv_buf[1024] = {0};
        
        DWORD result = IcmpSendEcho(icmp_handle,
                                     dest_addr.S_un.S_addr,
                                     send_data, sizeof(send_data),
                                     nullptr, recv_buf, sizeof(recv_buf),
                                     3000);
        
        if (result > 0) {
            PICMP_ECHO_REPLY reply = (PICMP_ECHO_REPLY)recv_buf;
            struct in_addr reply_addr;
            reply_addr.S_un.S_addr = reply->Address;
            
            char ip_str[INET_ADDRSTRLEN] = {0};
            inet_ntop(AF_INET, &reply_addr, ip_str, sizeof(ip_str));
            
            hop.ip_address = ip_str;
            hop.latency_ms = reply->RoundTripTime;
            hop.reachable = true;
            
            hops.push_back(hop);
            
            if (reply_addr.S_un.S_addr == dest_addr.S_un.S_addr) {
                break;
            }
        } else {
            hop.ip_address = "*";
            hop.reachable = false;
            hops.push_back(hop);
        }
    }
    
    IcmpCloseHandle(icmp_handle);
#endif
    
    return hops;
}

double RouteAnalyzer::measureLatency(const std::string& destination, uint32_t count) {
    double total_latency = 0.0;
    uint32_t successful = 0;
    
    for (uint32_t i = 0; i < count; i++) {
#ifdef PLATFORM_WINDOWS
        HANDLE icmp_handle = IcmpCreateFile();
        if (icmp_handle == INVALID_HANDLE_VALUE) continue;
        
        struct in_addr dest_addr;
        inet_pton(AF_INET, destination.c_str(), &dest_addr);
        
        char send_data[] = "GNO";
        char recv_buf[1024] = {0};
        
        DWORD result = IcmpSendEcho(icmp_handle,
                                     dest_addr.S_un.S_addr,
                                     send_data, sizeof(send_data),
                                     nullptr, recv_buf, sizeof(recv_buf),
                                     3000);
        
        if (result > 0) {
            PICMP_ECHO_REPLY reply = (PICMP_ECHO_REPLY)recv_buf;
            total_latency += reply->RoundTripTime;
            successful++;
        }
        
        IcmpCloseHandle(icmp_handle);
#endif
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    return successful > 0 ? total_latency / successful : -1.0;
}

double RouteAnalyzer::measurePacketLoss(const std::string& destination, uint32_t count) {
    uint32_t successful = 0;
    
    for (uint32_t i = 0; i < count; i++) {
#ifdef PLATFORM_WINDOWS
        HANDLE icmp_handle = IcmpCreateFile();
        if (icmp_handle == INVALID_HANDLE_VALUE) continue;
        
        struct in_addr dest_addr;
        inet_pton(AF_INET, destination.c_str(), &dest_addr);
        
        char send_data[] = "GNO";
        char recv_buf[1024] = {0};
        
        DWORD result = IcmpSendEcho(icmp_handle,
                                     dest_addr.S_un.S_addr,
                                     send_data, sizeof(send_data),
                                     nullptr, recv_buf, sizeof(recv_buf),
                                     3000);
        
        if (result > 0) {
            successful++;
        }
        
        IcmpCloseHandle(icmp_handle);
#endif
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    return count > 0 ? (1.0 - static_cast<double>(successful) / count) * 100.0 : 0.0;
}

bool RouteAnalyzer::setRoute(const std::string& destination, const std::string& gateway) {
#ifdef PLATFORM_WINDOWS
    MIB_IPFORWARDROW row = {0};
    struct in_addr dest_addr, gw_addr;
    
    inet_pton(AF_INET, destination.c_str(), &dest_addr);
    inet_pton(AF_INET, gateway.c_str(), &gw_addr);
    
    row.dwForwardDest = dest_addr.S_un.S_addr;
    row.dwForwardMask = 0;
    row.dwForwardPolicy = 0;
    row.dwForwardNextHop = gw_addr.S_un.S_addr;
    row.dwForwardIfIndex = 0;
    row.dwForwardType = 3;
    row.dwForwardProto = 3;
    row.dwForwardAge = 0;
    row.dwForwardNextHopAS = 0;
    row.dwForwardMetric1 = 1;
    
    DWORD result = CreateIpForwardEntry(&row);
    return (result == NO_ERROR);
#else
    return false;
#endif
}

bool RouteAnalyzer::deleteRoute(const std::string& destination) {
#ifdef PLATFORM_WINDOWS
    MIB_IPFORWARDROW row = {0};
    struct in_addr dest_addr;
    
    inet_pton(AF_INET, destination.c_str(), &dest_addr);
    row.dwForwardDest = dest_addr.S_un.S_addr;
    
    DWORD result = DeleteIpForwardEntry(&row);
    return (result == NO_ERROR);
#else
    return false;
#endif
}

void RouteAnalyzer::setLatencyCallback(LatencyCallback callback) {
    latency_callback_ = std::move(callback);
}

} // namespace gno
