#include "game_profiles.h"

#include "input_validation.h"
#include "json_persistence.h"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>

namespace {

constexpr std::size_t kMaxProfileBytes = 1024 * 1024;
constexpr std::size_t kMaxProfiles = 256;
constexpr int kProfileDocumentVersion = 1;

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
    if (profile.game_name.empty() || profile.game_name.size() > 128 ||
        profile.process_name.size() > 260 || profile.preferred_region.size() > 64) {
        throw std::runtime_error("invalid profile strings");
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
        if (root.is_array()) {
            items = &root; // Legacy documents are version 1.
        } else if (root.is_object() && root.contains("profiles") && root.at("profiles").is_array() &&
                   root.value("version", kProfileDocumentVersion) == kProfileDocumentVersion) {
            items = &root.at("profiles");
        }
        if (!items || items->size() > kMaxProfiles) return std::nullopt;

        std::vector<gno::GameProfile> result;
        result.reserve(items->size());
        for (const auto& item : *items) result.push_back(profileFromJson(item));
        return result;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

nlohmann::json profilesDocument(const std::vector<gno::GameProfile>& profiles) {
    nlohmann::json items = nlohmann::json::array();
    for (const auto& profile : profiles) items.push_back(profileToJson(profile));
    return {{"version", kProfileDocumentVersion}, {"profiles", std::move(items)}};
}

} // namespace

namespace gno {

GameProfiles::GameProfiles(std::filesystem::path storage_root)
    : storage_root_(std::move(storage_root)) {
    load();
}

std::string GameProfiles::getSavePath() const {
    return persistence::storageFile(storage_root_, "profiles.json").string();
}

bool GameProfiles::load() {
    const auto content = readBoundedFile(getSavePath(), kMaxProfileBytes);
    if (!content) return false;
    auto parsed = parseProfilesDocument(*content);
    if (!parsed) return false;
    profiles_ = std::move(*parsed);
    return true;
}

bool GameProfiles::saveProfiles(const std::vector<GameProfile>& profiles, const std::string& path) const {
    if (profiles.size() > kMaxProfiles) return false;
    return persistence::atomicWriteText(path, profilesDocument(profiles).dump(2));
}

bool GameProfiles::save() {
    return saveProfiles(profiles_, getSavePath());
}

bool GameProfiles::exportToFile(const std::string& path) const {
    return saveProfiles(profiles_, path);
}

bool GameProfiles::importFromFile(const std::string& path) {
    const auto content = readBoundedFile(path, kMaxProfileBytes);
    if (!content) return false;
    auto parsed = parseProfilesDocument(*content);
    if (!parsed || !saveProfiles(*parsed, getSavePath())) return false;
    profiles_ = std::move(*parsed);
    return true;
}

std::vector<GameProfile> GameProfiles::getAll() const { return profiles_; }

GameProfile GameProfiles::get(const std::string& game_name) const {
    for (const auto& profile : profiles_) {
        if (profile.game_name == game_name) return profile;
    }
    return GameProfile{};
}

bool GameProfiles::has(const std::string& game_name) const {
    return std::any_of(profiles_.begin(), profiles_.end(), [&game_name](const GameProfile& profile) {
        return profile.game_name == game_name;
    });
}

bool GameProfiles::set(const GameProfile& profile) {
    auto candidate = profiles_;
    const auto existing = std::find_if(candidate.begin(), candidate.end(), [&profile](const GameProfile& item) {
        return item.game_name == profile.game_name;
    });
    if (existing != candidate.end()) {
        *existing = profile;
    } else {
        if (candidate.size() >= kMaxProfiles) return false;
        candidate.push_back(profile);
    }
    if (!saveProfiles(candidate, getSavePath())) return false;
    profiles_ = std::move(candidate);
    return true;
}

void GameProfiles::remove(const std::string& game_name) {
    auto candidate = profiles_;
    candidate.erase(std::remove_if(candidate.begin(), candidate.end(), [&game_name](const GameProfile& profile) {
        return profile.game_name == game_name;
    }), candidate.end());
    if (saveProfiles(candidate, getSavePath())) profiles_ = std::move(candidate);
}

GameProfile GameProfiles::detectForProcess(const std::string& process_name) const {
    for (const auto& profile : profiles_) {
        if (profile.process_name == process_name) return profile;
    }
    return GameProfile{};
}

} // namespace gno
