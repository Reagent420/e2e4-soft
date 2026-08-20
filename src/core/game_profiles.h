#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <filesystem>
#include <functional>

namespace gno {

struct GameProfile {
    std::string game_name;
    std::string process_name;
    bool multipath_enabled = false;
    bool fps_boost_enabled = false;
    bool network_optimization = false;
    int max_routes = 3;
    std::string preferred_region = "auto";
    int priority_class = 6;
    std::vector<std::string> custom_routes;
    bool auto_apply = false;
};

class GameProfiles {
public:
    using TextWriter = std::function<bool(
        const std::filesystem::path&, const std::string&)>;

    explicit GameProfiles(
        std::filesystem::path storage_root = {}, TextWriter writer = {});
    ~GameProfiles() = default;

    bool load();
    bool save();

    std::vector<GameProfile> getAll() const;
    GameProfile get(const std::string& game_name) const;
    bool has(const std::string& game_name) const;

    bool set(const GameProfile& profile);
    void remove(const std::string& game_name);

    GameProfile detectForProcess(const std::string& process_name) const;

    std::string getSavePath() const;

    // Export/Import
    bool exportToFile(const std::string& path) const;
    bool importFromFile(const std::string& path);

private:
    bool saveProfiles(const std::vector<GameProfile>& profiles, const std::string& path) const;

    std::filesystem::path storage_root_;
    TextWriter writer_;
    std::vector<GameProfile> profiles_;
};

} // namespace gno
