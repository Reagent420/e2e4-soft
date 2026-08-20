#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace gno {

struct GameProfile {
    std::string game_name;
    std::string process_name;
    bool multipath_enabled = true;
    bool fps_boost_enabled = true;
    bool network_optimization = true;
    int max_routes = 3;
    std::string preferred_region = "auto";
    int priority_class = 6;
    std::vector<std::string> custom_routes;
    bool auto_apply = true;

    // granular per-game actions (v1.4)
    bool game_dvr_opt = true;
    bool power_plan_opt = true;
    bool high_priority_opt = true;
    bool tcp_opt = true;
    bool mtu_opt = true;
    bool custom_dns = false;
    std::string dns_server = "1.1.1.1";
    bool pro_config_opt = false;
    bool overlay_enabled = false;
};

class GameProfiles {
public:
    GameProfiles();
    ~GameProfiles();

    void load();
    void save();

    std::vector<GameProfile> getAll() const;
    GameProfile get(const std::string& game_name) const;
    bool has(const std::string& game_name) const;

    void set(const GameProfile& profile);
    void remove(const std::string& game_name);

    GameProfile detectForProcess(const std::string& process_name) const;

    std::string getSavePath() const;

    // Export/Import
    bool exportToFile(const std::string& path) const;
    bool importFromFile(const std::string& path);

private:
    static std::string getAppDataPath();
    GameProfile parseProfile(const std::string& json);
    static std::string escapeJson(const std::string& s);

    std::vector<GameProfile> profiles_;
};

} // namespace gno
