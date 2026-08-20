#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <functional>

#include "diagnostics/diagnostic_types.h"

namespace gno {

struct PacketLossResult {
    std::string target_ip;
    uint32_t packets_sent = 0;
    uint32_t packets_received = 0;
    uint32_t packets_lost = 0;
    double loss_percent = 0.0;
    double avg_loss_percent = 0.0;
    DiagnosticError error = DiagnosticError::InternalFailure;
};

class PacketLossMonitor {
public:
    using Probe = std::function<bool(const Ipv4Address&, uint32_t timeout_ms)>;

    explicit PacketLossMonitor(Probe probe = {});
    ~PacketLossMonitor() = default;

    PacketLossResult measure(const std::string& target_ip, uint32_t count = 50, uint32_t timeout_ms = 3000);

private:
    Probe probe_;

};

} // namespace gno
