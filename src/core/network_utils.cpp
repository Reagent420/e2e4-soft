#include "network_utils.h"
#include <thread>
#include <chrono>
#include <sstream>

#ifdef PLATFORM_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#include <shlobj.h>
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

// ---------------------------------------------------------------------------
// Local network improvements (MTU, TCP) — no VPN server needed
// ---------------------------------------------------------------------------

static std::wstring runCommand(const std::wstring& cmdline, std::string* output = nullptr) {
    // returns merged output; empty on failure
    std::wstring result;
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0))
        return result;
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.hStdInput = nullptr;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    std::wstring cmd = L"cmd.exe /c " + cmdline;
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(0);

    BOOL created = CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
                                  CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(writePipe);

    if (created) {
        CloseHandle(pi.hThread);
        // read output
        CHAR buf[4096];
        DWORD bytesRead = 0;
        std::string text;
        while (ReadFile(readPipe, buf, sizeof(buf), &bytesRead, nullptr) && bytesRead > 0) {
            text.append(buf, bytesRead);
        }
        CloseHandle(readPipe);
        WaitForSingleObject(pi.hProcess, 15000);
        CloseHandle(pi.hProcess);
        if (output) *output = text;
        result = text.empty() ? L"" : L"ok";
        return result;
    }

    CloseHandle(readPipe);
    return result;
}

static std::string getInterfaceFriendlyName() {
#ifdef PLATFORM_WINDOWS
    PIP_ADAPTER_ADDRESSES adapter_addresses = nullptr;
    ULONG size = 0;
    ULONG result = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, nullptr, nullptr, &size);

    if (result == ERROR_BUFFER_OVERFLOW) {
        adapter_addresses = (PIP_ADAPTER_ADDRESSES)malloc(size);
        result = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, nullptr, adapter_addresses, &size);

        if (result == NO_ERROR) {
            for (PIP_ADAPTER_ADDRESSES adapter = adapter_addresses; adapter; adapter = adapter->Next) {
                if (adapter->OperStatus == IfOperStatusUp && adapter->IfType != IF_TYPE_SOFTWARE_LOOPBACK) {
                    std::string name;
                    if (adapter->FriendlyName) {
                        int len = WideCharToMultiByte(CP_UTF8, 0, adapter->FriendlyName, -1, nullptr, 0, nullptr, nullptr);
                        if (len > 0) {
                            std::vector<char> buf(len);
                            WideCharToMultiByte(CP_UTF8, 0, adapter->FriendlyName, -1, buf.data(), len, nullptr, nullptr);
                            name = buf.data();
                        }
                    }
                    free(adapter_addresses);
                    return name;
                }
            }
        }
        free(adapter_addresses);
    }
#endif
    return "default";
}

std::string NetworkUtils::getNetworkInterfaceName() {
    std::string name = getInterfaceFriendlyName();
    if (name.empty() || name == "default")
        return "default";
    return name;
}

bool NetworkUtils::setDNS(const std::string& interface_name, const std::string& primary_dns, const std::string& secondary_dns) {
#ifdef PLATFORM_WINDOWS
    std::string iface = interface_name;
    if (iface.empty() || iface == "default")
        iface = getInterfaceFriendlyName();
    if (iface.empty() || iface == "default")
        return false;

    std::wstring ifaceW(iface.begin(), iface.end());

    if (primary_dns == "auto") {
        std::wstring cmd = L"netsh interface ip set dns name=\"" + ifaceW + L"\" source=dhcp";
        std::string out;
        runCommand(cmd, &out);
        return true;
    }

    std::wstring dnsW(primary_dns.begin(), primary_dns.end());
    std::wstring cmd = L"netsh interface ip set dns name=\"" + ifaceW + L"\" static " + dnsW;
    std::string out;
    runCommand(cmd, &out);

    if (!secondary_dns.empty() && secondary_dns != "auto") {
        std::wstring dns2W(secondary_dns.begin(), secondary_dns.end());
        std::wstring cmd2 = L"netsh interface ip add dns name=\"" + ifaceW + L"\" " + dns2W + L" index=2";
        std::string out2;
        runCommand(cmd2, &out2);
    }
    return true;
#else
    (void)interface_name; (void)primary_dns; (void)secondary_dns;
    return false;
#endif
}

bool NetworkUtils::setMTU(const std::string& interface_name, uint32_t mtu) {
#ifdef PLATFORM_WINDOWS
    std::string iface = interface_name;
    if (iface.empty() || iface == "default")
        iface = getInterfaceFriendlyName();
    if (iface.empty() || iface == "default")
        return false;
    if (mtu < 576 || mtu > 1500)
        return false;

    std::wstring ifaceW(iface.begin(), iface.end());
    std::wstring cmd = L"netsh interface ipv4 set subinterface \"" + ifaceW +
                       L"\" mtu=" + std::to_wstring(mtu) + L" store=persistent";
    std::string out;
    runCommand(cmd, &out);
    return true;
#else
    (void)interface_name; (void)mtu;
    return false;
#endif
}

bool NetworkUtils::getMTU(const std::string& interface_name, uint32_t& mtu_out) {
#ifdef PLATFORM_WINDOWS
    std::string iface = interface_name;
    if (iface.empty() || iface == "default")
        iface = getInterfaceFriendlyName();

    std::wstring ifaceW(iface.begin(), iface.end());
    std::wstring cmd = L"netsh interface ipv4 show subinterfaces";
    std::string out;
    runCommand(cmd, &out);

    // parse the line containing the interface name: "MTU  BytesIn  BytesOut  Interface"
    size_t pos = 0;
    while ((pos = out.find(iface, pos)) != std::string::npos) {
        // find the beginning of the line
        size_t lineStart = out.rfind('\n', pos);
        size_t lineEnd = out.find('\n', pos);
        std::string line = out.substr(lineStart == std::string::npos ? 0 : lineStart + 1,
                                      lineEnd == std::string::npos ? std::string::npos : lineEnd - (lineStart == std::string::npos ? 0 : lineStart + 1));
        std::istringstream iss(line);
        uint32_t mtu = 0;
        if (iss >> mtu) {
            mtu_out = mtu;
            return true;
        }
        pos += iface.size();
    }
#endif
    (void)interface_name;
    return false;
}

bool NetworkUtils::applyTCPOptimizations(bool enable) {
    bool ok = false;
#ifdef PLATFORM_WINDOWS
    HKEY hkey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER,
                      "Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings",
                      0, KEY_SET_VALUE, &hkey) == ERROR_SUCCESS) {
        if (enable) {
            DWORD one = 1;
            RegSetValueExA(hkey, "TcpAckFrequency", 0, REG_DWORD, (BYTE*)&one, sizeof(one));
            RegSetValueExA(hkey, "TCPNoDelay", 0, REG_DWORD, (BYTE*)&one, sizeof(one));
            RegSetValueExA(hkey, "TcpMaxDataRetransmissions", 0, REG_DWORD, (BYTE*)&one, sizeof(one));
        } else {
            RegDeleteValueA(hkey, "TcpAckFrequency");
            RegDeleteValueA(hkey, "TCPNoDelay");
            RegDeleteValueA(hkey, "TcpMaxDataRetransmissions");
        }
        RegCloseKey(hkey);
        ok = true;
    }

    // HKLM parameters need admin; try and ignore failure
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters",
                      0, KEY_SET_VALUE, &hkey) == ERROR_SUCCESS) {
        if (enable) {
            DWORD ttl = 64;
            DWORD tcp1323 = 1;
            RegSetValueExA(hkey, "DefaultTTL", 0, REG_DWORD, (BYTE*)&ttl, sizeof(ttl));
            RegSetValueExA(hkey, "Tcp1323Opts", 0, REG_DWORD, (BYTE*)&tcp1323, sizeof(tcp1323));
        } else {
            RegDeleteValueA(hkey, "DefaultTTL");
            RegDeleteValueA(hkey, "Tcp1323Opts");
        }
        RegCloseKey(hkey);
    }
#endif
    return ok;
}

uint32_t NetworkUtils::recommendMTU() {
    // 1500 (Ethernet) minus IP+ICMP headers gives a safe game MTU
    return 1400;
}

} // namespace gno
