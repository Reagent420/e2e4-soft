#pragma once

// Windows network I/O primitives for the v1.7.0 utilities.
// Thin, blocking, single-call helpers; threading belongs to the UI layer.

#include <cstdint>
#include <string>
#include <vector>

namespace gno {
namespace netscan {

struct IcmpResult {
    bool ok = false;
    double latency_ms = 0.0;
};

// ICMP echo with Don't-Fragment flag and explicit payload size.
IcmpResult icmpDfProbe(const std::string& ip, int payload_size, int timeout_ms);

// Blocking TCP connect attempt (true = port reachable).
bool tcpConnectCheck(const std::string& ip, std::uint16_t port, int timeout_ms);

// Local listening TCP ports owned by a process.
std::vector<std::uint16_t> listeningTcpPortsForPid(std::uint32_t pid);

// Established remote "ip:port" endpoints owned by a process.
std::vector<std::string> remoteTcpEndpointsForPid(std::uint32_t pid);

// One STUN binding exchange over UDP. Returns false on timeout/error.
bool stunExchange(const std::string& server_ip, std::uint16_t server_port,
                  const std::vector<std::uint8_t>& request, int timeout_ms,
                  std::vector<std::uint8_t>& response);

// Raw UDP DNS query to a specific resolver.
bool dnsQueryUdp(const std::string& dns_ip, const std::vector<std::uint8_t>& query,
                 int timeout_ms, std::vector<std::uint8_t>& response);

// System resolver (getaddrinfo) latency in ms; returns false on failure.
bool systemResolveMs(const std::string& host, double& out_ms);

// Saturating HTTP download for bufferbloat testing; returns bytes received.
std::size_t httpDownloadLoad(const std::string& host, const std::string& path, int seconds_cap);

std::string localIp();

} // namespace netscan
} // namespace gno
