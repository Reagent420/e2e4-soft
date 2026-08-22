#include "core/platform_netscan.h"

#include <chrono>
#include <cstring>

#ifdef PLATFORM_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <windows.h>
#endif

namespace gno {
namespace netscan {

#ifndef PLATFORM_WINDOWS

IcmpResult icmpDfProbe(const std::string&, int, int) { return {}; }
bool tcpConnectCheck(const std::string&, std::uint16_t, int) { return false; }
std::vector<std::uint16_t> listeningTcpPortsForPid(std::uint32_t) { return {}; }
std::vector<std::string> remoteTcpEndpointsForPid(std::uint32_t) { return {}; }
bool stunExchange(const std::string&, std::uint16_t, const std::vector<std::uint8_t>&, int,
                  std::vector<std::uint8_t>&) { return false; }
bool dnsQueryUdp(const std::string&, const std::vector<std::uint8_t>&, int,
                 std::vector<std::uint8_t>&) { return false; }
bool systemResolveMs(const std::string&, double&) { return false; }
std::size_t httpDownloadLoad(const std::string&, const std::string&, int) { return 0; }
std::string localIp() { return {}; }

#else

static void wsInit() {
    static bool done = false;
    if (!done) {
        WSADATA w;
        WSAStartup(MAKEWORD(2, 2), &w);
        done = true;
    }
}

IcmpResult icmpDfProbe(const std::string& ip, int payload_size, int timeout_ms) {
    IcmpResult r;
    wsInit();
    HANDLE h = IcmpCreateFile();
    if (h == INVALID_HANDLE_VALUE) return r;

    in_addr addr{};
    inet_pton(AF_INET, ip.c_str(), &addr);

    std::vector<char> send(static_cast<std::size_t>(payload_size), 'G');
    std::vector<char> reply(sizeof(ICMP_ECHO_REPLY) + static_cast<std::size_t>(payload_size) + 8);

    IP_OPTION_INFORMATION opts{};
    opts.Flags = IP_FLAG_DF; // don't fragment

    const auto t0 = std::chrono::steady_clock::now();
    const DWORD res = IcmpSendEcho(h, addr.S_un.S_addr, send.data(),
                                   static_cast<WORD>(send.size()), &opts, reply.data(),
                                   static_cast<DWORD>(reply.size()), static_cast<DWORD>(timeout_ms));
    const auto t1 = std::chrono::steady_clock::now();

    if (res > 0) {
        auto* er = reinterpret_cast<PICMP_ECHO_REPLY>(reply.data());
        if (er->Status == IP_SUCCESS) {
            r.ok = true;
            r.latency_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        }
    }
    IcmpCloseHandle(h);
    return r;
}

bool tcpConnectCheck(const std::string& ip, std::uint16_t port, int timeout_ms) {
    wsInit();
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;

    u_long nonblocking = 1;
    ioctlsocket(s, FIONBIO, &nonblocking);

    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &a.sin_addr);

    bool result = false;
    if (connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0) {
        result = true;
    } else {
        fd_set wset;
        FD_ZERO(&wset);
        FD_SET(s, &wset);
        timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
        if (select(0, nullptr, &wset, nullptr, &tv) > 0) {
            int err = 0, len = sizeof(err);
            getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len);
            result = err == 0;
        }
    }
    closesocket(s);
    return result;
}

template <typename TableT, typename RowT, typename Fn>
static std::vector<std::uint16_t> walkTcpTable(DWORD family, DWORD flags, std::uint32_t pid,
                                               bool listeners_only, Fn&& on_row) {
    std::vector<std::uint16_t> ports;
    DWORD size = 16 * 1024;
    std::vector<char> buf;
    for (int attempt = 0; attempt < 3; ++attempt) {
        buf.resize(size);
        const DWORD err = GetExtendedTcpTable(buf.data(), &size, FALSE, family, static_cast<TCP_TABLE_CLASS>(flags), 0);
        if (err == NO_ERROR) break;
        if (err == ERROR_INSUFFICIENT_BUFFER) continue;
        return ports;
    }
    auto* table = reinterpret_cast<TableT*>(buf.data());
    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        auto& row = *reinterpret_cast<RowT*>(&table->table[i]);
        if (row.dwOwningPid != pid) continue;
        const DWORD state = row.dwState;
        if (listeners_only && state != MIB_TCP_STATE_LISTEN) continue;
        on_row(row);
        ports.push_back(ntohs(static_cast<std::uint16_t>(row.dwLocalPort)));
    }
    return ports;
}

std::vector<std::uint16_t> listeningTcpPortsForPid(std::uint32_t pid) {
    wsInit();
    return walkTcpTable<MIB_TCPTABLE_OWNER_PID, MIB_TCPROW_OWNER_PID>(
        AF_INET, TCP_TABLE_OWNER_PID_LISTENER, pid, true, [](MIB_TCPROW_OWNER_PID&) {});
}

std::vector<std::string> remoteTcpEndpointsForPid(std::uint32_t pid) {
    wsInit();
    std::vector<std::string> out;
    DWORD size = 16 * 1024;
    std::vector<char> buf;
    for (int attempt = 0; attempt < 3; ++attempt) {
        buf.resize(size);
        const DWORD err =
            GetExtendedTcpTable(buf.data(), &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_CONNECTIONS, 0);
        if (err == NO_ERROR) break;
        if (err == ERROR_INSUFFICIENT_BUFFER) continue;
        return out;
    }
    auto* table = reinterpret_cast<MIB_TCPTABLE_OWNER_PID*>(buf.data());
    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const MIB_TCPROW_OWNER_PID& row = table->table[i];
        if (row.dwOwningPid != pid) continue;
        if (row.dwState != MIB_TCP_STATE_ESTAB) continue;
        char ip[32] = {};
        snprintf(ip, sizeof(ip), "%u.%u.%u.%u", row.dwRemoteAddr & 0xFF,
                 (row.dwRemoteAddr >> 8) & 0xFF, (row.dwRemoteAddr >> 16) & 0xFF,
                 (row.dwRemoteAddr >> 24) & 0xFF);
        out.emplace_back(std::string(ip) + ":" +
                         std::to_string(ntohs(static_cast<std::uint16_t>(row.dwRemotePort))));
    }
    return out;
}

bool stunExchange(const std::string& server_ip, std::uint16_t server_port,
                  const std::vector<std::uint8_t>& request, int timeout_ms,
                  std::vector<std::uint8_t>& response) {
    wsInit();
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return false;

    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip.c_str(), &a.sin_addr);

    bool ok = false;
    if (sendto(s, reinterpret_cast<const char*>(request.data()),
               static_cast<int>(request.size()), 0,
               reinterpret_cast<sockaddr*>(&a), sizeof(a)) == static_cast<int>(request.size())) {
        timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
        std::uint8_t buf[1024];
        const int n = recv(s, reinterpret_cast<char*>(buf), sizeof(buf), 0);
        if (n > 0) {
            response.assign(buf, buf + n);
            ok = true;
        }
    }
    closesocket(s);
    return ok;
}

bool dnsQueryUdp(const std::string& dns_ip, const std::vector<std::uint8_t>& query,
                 int timeout_ms, std::vector<std::uint8_t>& response) {
    return stunExchange(dns_ip, 53, query, timeout_ms, response); // same UDP request/response shape
}

bool systemResolveMs(const std::string& host, double& out_ms) {
    wsInit();
    addrinfo hints{};
    hints.ai_family = AF_INET;
    const auto t0 = std::chrono::steady_clock::now();
    addrinfo* res = nullptr;
    const int rc = getaddrinfo(host.c_str(), nullptr, &hints, &res);
    const auto t1 = std::chrono::steady_clock::now();
    if (rc != 0 || !res) return false;
    freeaddrinfo(res);
    out_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return true;
}

std::size_t httpDownloadLoad(const std::string& host, const std::string& path, int seconds_cap) {
    wsInit();
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), "80", &hints, &res) != 0 || !res) return 0;

    SOCKET s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    std::size_t bytes = 0;
    if (s != INVALID_SOCKET &&
        connect(s, res->ai_addr, static_cast<int>(res->ai_addrlen)) == 0) {
        const std::string req = "GET " + path + " HTTP/1.1\r\nHost: " + host +
                                "\r\nConnection: close\r\nUser-Agent: GNO/1.7\r\n\r\n";
        send(s, req.c_str(), static_cast<int>(req.size()), 0);
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(seconds_cap);
        char buf[16384];
        while (std::chrono::steady_clock::now() < deadline) {
            const int n = recv(s, buf, sizeof(buf), 0);
            if (n <= 0) break;
            bytes += static_cast<std::size_t>(n);
        }
    }
    if (s != INVALID_SOCKET) closesocket(s);
    freeaddrinfo(res);
    return bytes;
}

std::string localIp() {
    wsInit();
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return {};
    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(53);
    inet_pton(AF_INET, "8.8.8.8", &remote.sin_addr);
    std::string out;
    if (connect(s, reinterpret_cast<sockaddr*>(&remote), sizeof(remote)) == 0) {
        sockaddr_in local{};
        int len = sizeof(local);
        getsockname(s, reinterpret_cast<sockaddr*>(&local), &len);
        char ip[32] = {};
        snprintf(ip, sizeof(ip), "%u.%u.%u.%u", local.sin_addr.S_un.S_addr & 0xFF,
                 (local.sin_addr.S_un.S_addr >> 8) & 0xFF,
                 (local.sin_addr.S_un.S_addr >> 16) & 0xFF,
                 (local.sin_addr.S_un.S_addr >> 24) & 0xFF);
        out = ip;
    }
    closesocket(s);
    return out;
}

#endif // PLATFORM_WINDOWS

} // namespace netscan
} // namespace gno
