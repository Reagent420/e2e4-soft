#include "icmp_probe.h"

#include <chrono>

#ifdef PLATFORM_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#endif

namespace gno {

IcmpPing defaultIcmpPing() {
    return [](const std::string& target_ip, int timeout_ms) -> double {
#ifdef PLATFORM_WINDOWS
        // icmp-дескриптор — единственный ресурс; закрывается на любом пути
        // выхода, иначе handle утекал на каждый невалидный адрес.
        HANDLE icmp = IcmpCreateFile();
        if (icmp == INVALID_HANDLE_VALUE) return -1.0;

        // inet_pton явно проверяется: раньше при сбое разбора в addr оставался
        // мусор и уходил в IcmpSendEcho, что могло дать «успешный» пинг
        // на бессмысленный адрес.
        struct in_addr addr{};
        if (inet_pton(AF_INET, target_ip.c_str(), &addr) != 1) {
            IcmpCloseHandle(icmp);
            return -1.0;
        }

        char send[] = "GNO";
        char recv_buf[1024] = {};

        const auto start = std::chrono::steady_clock::now();
        const DWORD reply = IcmpSendEcho(icmp, addr.S_un.S_addr,
                                         send, sizeof(send), nullptr,
                                         recv_buf, sizeof(recv_buf),
                                         static_cast<DWORD>(timeout_ms));
        const double ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - start).count();

        IcmpCloseHandle(icmp);

        // IcmpSendEcho возвращает количество ответов; 0 — это таймаут или
        // ошибка (IP_REQ_TIMED_OUT и т.п.), а не «ответ с нулевой задержкой».
        if (reply == 0) return -1.0;
        return ms;
#else
        (void)target_ip;
        (void)timeout_ms;
        return -1.0;
#endif
    };
}

} // namespace gno
