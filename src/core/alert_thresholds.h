#pragma once

// Per-game alert thresholds for tray/status degradation warnings.
// Games differ: VALORANT players feel 40 ms; PUBG tolerates more.

#include <string>

namespace gno {

struct AlertThresholds {
    double max_ping_ms = 100.0;
    double max_loss_percent = 3.0;

    static AlertThresholds forGame(const std::string& game_name) {
        AlertThresholds t;
        auto contains = [&game_name](const char* part) {
            return game_name.find(part) != std::string::npos;
        };
        if (contains("VALORANT") || contains("Valorant")) {
            t.max_ping_ms = 50.0;
            t.max_loss_percent = 1.0;
        } else if (contains("Counter-Strike") || game_name == "CS2") {
            t.max_ping_ms = 60.0;
            t.max_loss_percent = 1.5;
        } else if (contains("Dota")) {
            t.max_ping_ms = 80.0;
            t.max_loss_percent = 2.0;
        } else if (contains("PUBG")) {
            t.max_ping_ms = 100.0;
            t.max_loss_percent = 3.0;
        }
        // default (unknown game): 100 / 3
        return t;
    }

    bool isDegraded(double ping_ms, double loss_percent) const {
        return ping_ms > max_ping_ms || loss_percent > max_loss_percent;
    }
};

} // namespace gno
