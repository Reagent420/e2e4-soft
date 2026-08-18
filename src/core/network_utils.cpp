#include "network_utils.h"
#include <thread>
#include <chrono>

#ifdef PLATFORM_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#endif

namespace gno {

std::string NetworkUtils::getLocalIPAddress() {
#ifdef PLATFORM_WINDOWS
    char hostname[256] = {0};
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        struct addrinfo hints = {}, *result = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        
        if (getaddrinfo(hostname, nullptr, &hints, &result) == 0) {
            char ip_str[INET_ADDRSTRLEN] = {0};
            struct sockaddr_in* addr = (struct sockaddr_in*)result->ai_addr;
            inet_ntop(AF_INET, &addr->sin_addr, ip_str, sizeof(ip_str));
            freeaddrinfo(result);
            return ip_str;
        }
    }
#endif
    return "127.0.0.1";
}

std::string NetworkUtils::getPublicIPAddress() {
    return "0.0.0.0";
}

std::vector<std::string> NetworkUtils::resolveDNS(const std::string& hostname) {
    std::vector<std::string> addresses;
    
    struct addrinfo hints = {}, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    
    if (getaddrinfo(hostname.c_str(), nullptr, &hints, &result) == 0) {
        for (struct addrinfo* ptr = result; ptr; ptr = ptr->ai_next) {
            char ip_str[INET_ADDRSTRLEN] = {0};
            struct sockaddr_in* addr = (struct sockaddr_in*)ptr->ai_addr;
            inet_ntop(AF_INET, &addr->sin_addr, ip_str, sizeof(ip_str));
            addresses.push_back(ip_str);
        }
        freeaddrinfo(result);
    }
    
    return addresses;
}

std::string NetworkUtils::reverseDNS(const std::string& ip) {
    struct in_addr addr;
    inet_pton(AF_INET, ip.c_str(), &addr);
    
    struct sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_addr = addr;
    
    char hostname[256] = {0};
    if (getnameinfo((struct sockaddr*)&sa, sizeof(sa), hostname, sizeof(hostname), nullptr, 0, 0) == 0) {
        return hostname;
    }
    
    return "";
}

bool NetworkUtils::isPortOpen(const std::string& host, uint16_t port, uint32_t timeout_ms) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
    
#ifdef PLATFORM_WINDOWS
    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);
#else
    fcntl(sock, F_SETFL, O_NONBLOCK);
#endif
    
    connect(sock, (struct sockaddr*)&addr, sizeof(addr));
    
    fd_set writefds;
    FD_ZERO(&writefds);
    FD_SET(sock, &writefds);
    
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    
    int result = select(sock + 1, nullptr, &writefds, nullptr, &tv);
    
#ifdef PLATFORM_WINDOWS
    closesocket(sock);
#else
    close(sock);
#endif
    
    return result > 0;
}

std::vector<uint16_t> NetworkUtils::scanPorts(const std::string& host, uint16_t start_port, uint16_t end_port, uint32_t timeout_ms) {
    std::vector<uint16_t> open_ports;
    
    for (uint16_t port = start_port; port <= end_port; port++) {
        if (isPortOpen(host, port, timeout_ms)) {
            open_ports.push_back(port);
        }
    }
    
    return open_ports;
}

double NetworkUtils::measureBandwidth(const std::string& server, bool download) {
    return 0.0;
}

std::string NetworkUtils::getNetworkInterfaceName() {
    return "default";
}

bool NetworkUtils::setDNS(const std::string& interface_name, const std::string& primary_dns, const std::string& secondary_dns) {
#ifdef PLATFORM_WINDOWS
    PIP_ADAPTER_ADDRESSES adapter_addresses = nullptr;
    ULONG size = 0;
    
    GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, nullptr, nullptr, &size);
    adapter_addresses = (PIP_ADAPTER_ADDRESSES)malloc(size);
    
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, nullptr, adapter_addresses, &size) == NO_ERROR) {
        for (PIP_ADAPTER_ADDRESSES adapter = adapter_addresses; adapter; adapter = adapter->Next) {
            if (adapter->OperStatus == IfOperStatusUp) {
                PIP_ADAPTER_DNS_SERVER_ADDRESS dns = adapter->FirstDnsServerAddress;
                if (dns) {
                    free(adapter_addresses);
                    return true;
                }
            }
        }
    }
    
    free(adapter_addresses);
#endif
    return false;
}

bool NetworkUtils::resetDNS(const std::string& interface_name) {
    return setDNS(interface_name, "auto", "auto");
}

} // namespace gno
