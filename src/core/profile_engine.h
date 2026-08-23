#pragma once

// Profiles 2.0 (v1.9.0): single source of truth that merges per-game action
// toggles (GameProfile) with quality thresholds (GameModeCatalog) into one
// portable document. Serialization is a small INI-like text format
// ("GNO profile v1") - deterministic, human-editable, fully testable.

#include "core/game_profiles.h"

#include <sstream>
#include <string>
#include <vector>

namespace gno {

struct ProfileDocument {
    // identity
    std::string game_name;
    std::string process_name;

    // actions
    bool multipath = true;
    bool fps_boost = true;
    bool network_opt = true;
    bool auto_apply = true;
    bool game_dvr = true;
    bool power_plan = true;
    bool high_priority = true;
    bool tcp = true;
    bool mtu = true;
    bool custom_dns = false;
    std::string dns_server = "1.1.1.1";

    // thresholds
    double max_median_rtt = 60.0;
    double max_jitter = 8.0;
    double max_packet_loss = 2.0;
};

class ProfileEngine {
public:
    static ProfileDocument fromGameProfile(const GameProfile& p,
                                           double max_rtt, double max_jitter, double max_loss) {
        ProfileDocument d;
        d.game_name = p.game_name;
        d.process_name = p.process_name;
        d.multipath = p.multipath_enabled;
        d.fps_boost = p.fps_boost_enabled;
        d.network_opt = p.network_optimization;
        d.auto_apply = p.auto_apply;
        d.game_dvr = p.game_dvr_opt;
        d.power_plan = p.power_plan_opt;
        d.high_priority = p.high_priority_opt;
        d.tcp = p.tcp_opt;
        d.mtu = p.mtu_opt;
        d.custom_dns = p.custom_dns;
        d.dns_server = p.dns_server;
        d.max_median_rtt = max_rtt;
        d.max_jitter = max_jitter;
        d.max_packet_loss = max_loss;
        return d;
    }

    static std::string toIni(const ProfileDocument& d) {
        std::ostringstream o;
        o << "GNO profile v1\n";
        o << "game=" << d.game_name << "\n";
        o << "process=" << d.process_name << "\n";
        o << "multipath=" << d.multipath << "\n";
        o << "fps_boost=" << d.fps_boost << "\n";
        o << "network_opt=" << d.network_opt << "\n";
        o << "auto_apply=" << d.auto_apply << "\n";
        o << "game_dvr=" << d.game_dvr << "\n";
        o << "power_plan=" << d.power_plan << "\n";
        o << "high_priority=" << d.high_priority << "\n";
        o << "tcp=" << d.tcp << "\n";
        o << "mtu=" << d.mtu << "\n";
        o << "custom_dns=" << d.custom_dns << "\n";
        o << "dns_server=" << d.dns_server << "\n";
        o << "max_median_rtt=" << d.max_median_rtt << "\n";
        o << "max_jitter=" << d.max_jitter << "\n";
        o << "max_packet_loss=" << d.max_packet_loss << "\n";
        return o.str();
    }

    // Strict header check; unknown keys are ignored; missing keys keep defaults.
    static bool fromIni(const std::string& text, ProfileDocument& out) {
        if (text.rfind("GNO profile v1", 0) != 0) return false;
        std::istringstream ss(text);
        std::string line;
        while (std::getline(ss, line)) {
            if (line.empty() || line[0] == '#') continue;
            const auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = line.substr(0, eq);
            const std::string val = line.substr(eq + 1);
            auto b = [&]() { return val == "1" || val == "true"; };
            if (key == "game") out.game_name = val;
            else if (key == "process") out.process_name = val;
            else if (key == "multipath") out.multipath = b();
            else if (key == "fps_boost") out.fps_boost = b();
            else if (key == "network_opt") out.network_opt = b();
            else if (key == "auto_apply") out.auto_apply = b();
            else if (key == "game_dvr") out.game_dvr = b();
            else if (key == "power_plan") out.power_plan = b();
            else if (key == "high_priority") out.high_priority = b();
            else if (key == "tcp") out.tcp = b();
            else if (key == "mtu") out.mtu = b();
            else if (key == "custom_dns") out.custom_dns = b();
            else if (key == "dns_server") out.dns_server = val;
            else if (key == "max_median_rtt") { try { out.max_median_rtt = std::stod(val); } catch (...) {} }
            else if (key == "max_jitter") { try { out.max_jitter = std::stod(val); } catch (...) {} }
            else if (key == "max_packet_loss") { try { out.max_packet_loss = std::stod(val); } catch (...) {} }
            // unknown keys silently ignored - forward compatibility
        }
        return !out.game_name.empty();
    }

    // Applies action flags back onto a GameProfile (thresholds stay in catalog).
    static void applyToGameProfile(const ProfileDocument& d, GameProfile& p) {
        p.game_name = d.game_name;
        p.process_name = d.process_name;
        p.multipath_enabled = d.multipath;
        p.fps_boost_enabled = d.fps_boost;
        p.network_optimization = d.network_opt;
        p.auto_apply = d.auto_apply;
        p.game_dvr_opt = d.game_dvr;
        p.power_plan_opt = d.power_plan;
        p.high_priority_opt = d.high_priority;
        p.tcp_opt = d.tcp;
        p.mtu_opt = d.mtu;
        p.custom_dns = d.custom_dns;
        p.dns_server = d.dns_server;
    }
};

} // namespace gno
