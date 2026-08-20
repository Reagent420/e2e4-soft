#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gno {

struct NetworkUtils {
    static std::string getLocalIPAddress();
    static std::string getPublicIPAddress();
    static std::vector<std::string> resolveDNS(const std::string& hostname);
    static std::string reverseDNS(const std::string& ip);
    
    static bool isPortOpen(const std::string& host, uint16_t port, uint32_t timeout_ms = 3000);
    static std::vector<uint16_t> scanPorts(const std::string& host, uint16_t start_port, uint16_t end_port, uint32_t timeout_ms = 1000);
    
    static double measureBandwidth(const std::string& server, bool download = true);
    static std::string getNetworkInterfaceName();
};

} // namespace gno
