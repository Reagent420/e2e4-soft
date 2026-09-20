#pragma once

// Тонкий слой над ICMP-эхо, общий для DNSManager (бенчмарк DNS-серверов) и
// SpeedTest (пинг игровых серверов). Раньше каждый из них дублировал один и
// тот же блок IcmpCreateFile / inet_pton / IcmpSendEcho прямо в теле функции —
// а значит, его невозможно было проверить без реальной сети: в sandbox ICMP
// либо подменяется прокси, либо молча отваливается по таймауту, и оба класса
// просто возвращали success=false ни из-за чего.
//
// Теперь оба класса принимают реализацию через setPingProbe() — точно так же,
// как уже сделано в MultipathEngine (setPathProbe) и DiagnosticsService.
// Продуктовый код использует defaultIcmpPing(), тесты — свою реализацию.

#include <functional>
#include <string>

namespace gno {

// Один эхо-запрос по адресу target_ip с таймаутом timeout_ms.
// Возвращает latency в миллисекундах либо -1.0, если ответа нет
// (таймаут, невалидный адрес, ошибка ICMP-сокета). Именно отрицательное
// значение, а не 0.0: ноль — это валидный «мгновенный» ответ (localhost).
using IcmpPing = std::function<double(const std::string& target_ip, int timeout_ms)>;

// Реализация на настоящих IcmpSendEcho (Windows). На других платформах
// всегда отвечает -1.0 — ICMP-эхо там требует raw-сокетов, которых нет
// без root, а фолбэчить на TCP-коннект было бы ложью про «пинг».
IcmpPing defaultIcmpPing();

} // namespace gno
