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

private:
    std::vector<GameProfile> profiles_;
};

} // namespace gno
