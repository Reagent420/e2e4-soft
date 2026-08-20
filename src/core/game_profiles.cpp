#include "game_profiles.h"

#include "input_validation.h"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

#ifdef PLATFORM_WINDOWS
#include <shlobj.h>
#include <windows.h>
#endif

namespace {

constexpr std::size_t kMaxProfileBytes = 1024 * 1024;
constexpr std::size_t kMaxProfiles = 256;

gno::GameProfile profileFromJson(const nlohmann::json& value) {
    gno::GameProfile profile;
    profile.game_name = value.at("game_name").get<std::string>();
    profile.process_name = value.at("process_name").get<std::string>();
    profile.multipath_enabled = value.value("multipath_enabled", false);
    profile.fps_boost_enabled = value.value("fps_boost_enabled", false);
    profile.network_optimization = value.value("network_optimization", false);
    profile.max_routes = std::clamp(value.value("max_routes", 1), 1, 5);
    profile.preferred_region = value.value("preferred_region", std::string{"auto"});
    profile.priority_class = std::clamp(value.value("priority_class", 0), 0, 10);
    profile.auto_apply = value.value("auto_apply", false);
    if (profile.game_name.size() > 128 || profile.process_name.size() > 260 ||
        profile.preferred_region.size() > 64) {
        throw std::runtime_error("profile string too long");
    }
    if (value.contains("custom_routes") &&
        (!value.at("custom_routes").is_array() || !value.at("custom_routes").empty())) {
        throw std::runtime_error("imported custom routes are not permitted");
    }
    return profile;
}

nlohmann::json profileToJson(const gno::GameProfile& profile) {
    return {{"game_name", profile.game_name},
            {"process_name", profile.process_name},
            {"multipath_enabled", profile.multipath_enabled},
            {"fps_boost_enabled", profile.fps_boost_enabled},
            {"network_optimization", profile.network_optimization},
            {"max_routes", profile.max_routes},
            {"preferred_region", profile.preferred_region},
            {"priority_class", profile.priority_class},
            {"auto_apply", profile.auto_apply}};
}

std::optional<std::vector<gno::GameProfile>> parseProfilesDocument(const std::string& content) {
    try {
        const auto root = nlohmann::json::parse(content);
        const nlohmann::json* items = nullptr;
        if (root.is_array()) items = &root;
        if (root.is_object() && root.contains("profiles") && root.at("profiles").is_array()) {
            items = &root.at("profiles");
        }
        if (!items || items->size() > kMaxProfiles) return std::nullopt;

        std::vector<gno::GameProfile> result;
        result.reserve(items->size());
        for (const auto& item : *items) {
            auto profile = profileFromJson(item);
            if (profile.game_name.empty()) return std::nullopt;
            result.push_back(std::move(profile));
        }
        return result;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

} // namespace

namespace gno {

std::string GameProfiles::getAppDataPath() {
#ifdef PLATFORM_WINDOWS
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, path))) {
        return std::string(path);
    }
#endif
    return ".";
}

GameProfiles::GameProfiles() {
    load();
}

GameProfiles::~GameProfiles() {
    save();
}

std::string GameProfiles::getSavePath() const {
    return getAppDataPath() + "\\GNO\\profiles.json";
}

void GameProfiles::load() {
    const auto content = readBoundedFile(getSavePath(), kMaxProfileBytes);
    if (!content) return;

    auto parsed = parseProfilesDocument(*content);
    if (parsed) profiles_ = std::move(*parsed);
}

bool GameProfiles::save() {
    const std::string path = getSavePath();
    const std::string dir = path.substr(0, path.find_last_of("\\/"));
#ifdef PLATFORM_WINDOWS
    CreateDirectoryA(dir.c_str(), nullptr);
#endif

    nlohmann::json items = nlohmann::json::array();
    for (const auto& profile : profiles_) items.push_back(profileToJson(profile));

    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << items.dump(2);
    return static_cast<bool>(file);
}

bool GameProfiles::exportToFile(const std::string& path) const {
    const std::string dir = path.substr(0, path.find_last_of("\\/"));
#ifdef PLATFORM_WINDOWS
    CreateDirectoryA(dir.c_str(), nullptr);
#endif

    nlohmann::json items = nlohmann::json::array();
    for (const auto& profile : profiles_) items.push_back(profileToJson(profile));

    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << nlohmann::json{{"version", 1}, {"profiles", items}}.dump(2);
    return static_cast<bool>(file);
}

bool GameProfiles::importFromFile(const std::string& path) {
    const auto content = readBoundedFile(path, kMaxProfileBytes);
    if (!content) return false;

    auto parsed = parseProfilesDocument(*content);
    if (!parsed) return false;

    profiles_ = std::move(*parsed);
    return save();
}

std::vector<GameProfile> GameProfiles::getAll() const { return profiles_; }

GameProfile GameProfiles::get(const std::string& game_name) const {
    for (const auto& profile : profiles_) {
        if (profile.game_name == game_name) return profile;
    }
    return GameProfile{};
}

bool GameProfiles::has(const std::string& game_name) const {
    for (const auto& profile : profiles_) {
        if (profile.game_name == game_name) return true;
    }
    return false;
}

void GameProfiles::set(const GameProfile& profile) {
    for (auto& existing : profiles_) {
        if (existing.game_name == profile.game_name) {
            existing = profile;
            save();
            return;
        }
    }
    profiles_.push_back(profile);
    save();
}

void GameProfiles::remove(const std::string& game_name) {
    profiles_.erase(
        std::remove_if(profiles_.begin(), profiles_.end(),
                       [&game_name](const GameProfile& profile) { return profile.game_name == game_name; }),
        profiles_.end());
    save();
}

GameProfile GameProfiles::detectForProcess(const std::string& process_name) const {
    for (const auto& profile : profiles_) {
        if (profile.process_name == process_name) return profile;
    }
    return GameProfile{};
}

} // namespace gno
