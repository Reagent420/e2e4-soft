#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <optional>
#include <functional>
#include <cstdint>
#include <memory>

namespace gno {

struct RouteInfo {
    std::string interface_name;
    std::string gateway_ip;
    std::string source_ip;
    std::string destination_ip;
    uint32_t metric = 0;
    uint32_t mtu = 1500;
    uint32_t interface_index = 0;
    double latency_ms = 0.0;
    double packet_loss_percent = 0.0;
    bool is_active = false;
};

struct HopInfo {
    uint32_t hop_number = 0;
    std::string ip_address;
    std::string hostname;
    double latency_ms = 0.0;
    double jitter_ms = 0.0;
    bool reachable = false;
};

struct RoutePath {
    std::vector<HopInfo> hops;
    double total_latency_ms = 0.0;
    double total_jitter_ms = 0.0;
    double packet_loss_percent = 0.0;
    uint32_t path_id = 0;
    bool is_optimal = false;
};

struct NetworkInterface {
    std::string name;
    std::string description;
    std::string ip_address;
    std::string subnet_mask;
    std::string gateway;
    std::string dns_server;
    uint32_t mtu = 1500;
    bool is_up = false;
    bool is_wireless = false;
};

class RouteAnalyzer {
public:
    RouteAnalyzer();
    ~RouteAnalyzer();

    std::vector<RouteInfo> getRoutes();
    std::vector<NetworkInterface> getInterfaces();
    std::optional<RouteInfo> getRouteTo(const std::string& destination);
    std::vector<HopInfo> traceroute(const std::string& destination, uint32_t max_hops = 30);
    
    double measureLatency(const std::string& destination, uint32_t count = 10);
    double measurePacketLoss(const std::string& destination, uint32_t count = 100);

    using LatencyCallback = std::function<void(const std::string& ip, double latency_ms)>;
    void setLatencyCallback(LatencyCallback callback);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    LatencyCallback latency_callback_;
};

} // namespace gno
