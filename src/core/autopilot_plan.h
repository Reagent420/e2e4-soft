#pragma once

// Maps a game profile's toggles onto legacy fix-action ids executed through
// the transactional bridge (LaunchDiagnostics::applyFix -> safe engine).

#include "core/game_profiles.h"

#include <string>
#include <vector>

namespace gno {

struct AutopilotPlan {
    static std::vector<std::string> actionIdsFor(const GameProfile& p) {
        std::vector<std::string> ids;
        if (p.power_plan_opt) ids.push_back("power_plan");
        if (p.game_dvr_opt) ids.push_back("game_dvr");
        if (p.tcp_opt) ids.push_back("tcp");
        if (p.mtu_opt) ids.push_back("mtu");
        if (p.custom_dns) ids.push_back("dns");
        if (p.high_priority_opt) ids.push_back("priority");
        return ids;
    }

    static const char* displayName(const std::string& id) {
        if (id == "power_plan") return "Power plan";
        if (id == "game_dvr") return "Game DVR";
        if (id == "tcp") return "TCP";
        if (id == "mtu") return "MTU";
        if (id == "dns") return "DNS";
        if (id == "priority") return "Priority";
        return id.c_str();
    }
};

} // namespace gno
