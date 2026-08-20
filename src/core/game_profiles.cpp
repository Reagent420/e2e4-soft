#include "game_profiles.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <filesystem>

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#include <shlobj.h>
#endif

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
    profiles_.clear();

    std::ifstream file(getSavePath());
    if (!file.is_open()) return;

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (content.empty()) return;

    // Simple JSON parsing
    std::string trimmed = content;
    trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n["));
    trimmed.erase(trimmed.find_last_not_of(" \t\r\n]") + 1);
    
    size_t pos = 0;
    while (pos < trimmed.size()) {
        // Find next object
        size_t objStart = trimmed.find('{', pos);
        if (objStart == std::string::npos) break;
        size_t objEnd = trimmed.find('}', objStart);
        if (objEnd == std::string::npos) break;
        
        std::string obj = trimmed.substr(objStart, objEnd - objStart + 1);
        GameProfile p = parseProfile(obj);
        if (!p.game_name.empty()) {
            profiles_.push_back(p);
        }
        pos = objEnd + 1;
    }
}

GameProfile GameProfiles::parseProfile(const std::string& json) {
    GameProfile p;
    auto extractString = [&](const std::string& key) -> std::string {
        std::string search = "\"" + key + "\"";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return "";
        pos = json.find(':', pos);
        if (pos == std::string::npos) return "";
        pos = json.find('"', pos);
        if (pos == std::string::npos) return "";
        size_t end = json.find('"', pos + 1);
        if (end == std::string::npos) return "";
        return json.substr(pos + 1, end - pos - 1);
    };
    
    auto extractBool = [&](const std::string& key) -> bool {
        std::string search = "\"" + key + "\"";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return false;
        pos = json.find(':', pos);
        if (pos == std::string::npos) return false;
        std::string val = json.substr(pos + 1, 5);
        return val.find("true") != std::string::npos;
    };
    
    auto extractInt = [&](const std::string& key) -> int {
        std::string search = "\"" + key + "\"";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return 0;
        pos = json.find(':', pos);
        if (pos == std::string::npos) return 0;
        size_t end = json.find_first_of(",}", pos);
        if (end == std::string::npos) return 0;
        std::string val = json.substr(pos + 1, end - pos - 1);
        try { return std::stoi(val); } catch (...) { return 0; }
    };
    
    p.game_name = extractString("game_name");
    p.process_name = extractString("process_name");
    p.multipath_enabled = extractBool("multipath_enabled");
    p.fps_boost_enabled = extractBool("fps_boost_enabled");
    p.network_optimization = extractBool("network_optimization");
    p.max_routes = extractInt("max_routes");
    p.auto_apply = extractBool("auto_apply");
    return p;
}

void GameProfiles::save() {
    std::string path = getSavePath();

    std::string dir = path.substr(0, path.find_last_of("\\/"));
#ifdef PLATFORM_WINDOWS
    CreateDirectoryA(dir.c_str(), nullptr);
#endif

    std::ofstream file(path);
    if (!file.is_open()) return;

    file << "[\n";
    for (size_t i = 0; i < profiles_.size(); ++i) {
        const auto& p = profiles_[i];
        file << "  {\n";
        file << "    \"game_name\": \"" << GameProfiles::escapeJson(p.game_name) << "\",\n";
        file << "    \"process_name\": \"" << GameProfiles::escapeJson(p.process_name) << "\",\n";
        file << "    \"multipath_enabled\": " << (p.multipath_enabled ? "true" : "false") << ",\n";
        file << "    \"fps_boost_enabled\": " << (p.fps_boost_enabled ? "true" : "false") << ",\n";
        file << "    \"network_optimization\": " << (p.network_optimization ? "true" : "false") << ",\n";
        file << "    \"max_routes\": " << p.max_routes << ",\n";
        file << "    \"auto_apply\": " << (p.auto_apply ? "true" : "false") << "\n";
        file << "  }";
        if (i < profiles_.size() - 1) file << ",";
        file << "\n";
    }
    file << "]\n";
}

std::string GameProfiles::escapeJson(const std::string& s) {
    std::string result;
    for (char c : s) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c; break;
        }
    }
    return result;
}

bool GameProfiles::exportToFile(const std::string& path) const {
    std::string dir = path.substr(0, path.find_last_of("\\/"));
#ifdef PLATFORM_WINDOWS
    CreateDirectoryA(dir.c_str(), nullptr);
#endif

    std::ofstream file(path);
    if (!file.is_open()) return false;

    file << "{\n";
    file << "  \"version\": 1,\n";
    file << "  \"profiles\": [\n";
    for (size_t i = 0; i < profiles_.size(); ++i) {
        const auto& p = profiles_[i];
        file << "    {\n";
        file << "      \"game_name\": \"" << GameProfiles::escapeJson(p.game_name) << "\",\n";
        file << "      \"process_name\": \"" << GameProfiles::escapeJson(p.process_name) << "\",\n";
        file << "      \"multipath_enabled\": " << (p.multipath_enabled ? "true" : "false") << ",\n";
        file << "      \"fps_boost_enabled\": " << (p.fps_boost_enabled ? "true" : "false") << ",\n";
        file << "      \"network_optimization\": " << (p.network_optimization ? "true" : "false") << ",\n";
        file << "      \"max_routes\": " << p.max_routes << ",\n";
        file << "      \"auto_apply\": " << (p.auto_apply ? "true" : "false") << "\n";
        file << "    }";
        if (i < profiles_.size() - 1) file << ",";
        file << "\n";
    }
    file << "  ]\n";
    file << "}\n";
    return true;
}

bool GameProfiles::importFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;
    
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (content.empty()) return false;
    
    // Parse the JSON (supports both old array format and new object format)
    std::string trimmed = content;
    trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n[{"));
    trimmed.erase(trimmed.find_last_not_of(" \t\r\n}]") + 1);
    
    // Remove outer object wrapper if present
    if (trimmed.find("\"profiles\"") != std::string::npos) {
        size_t arrStart = trimmed.find("[");
        size_t arrEnd = trimmed.rfind("]");
        if (arrStart != std::string::npos && arrEnd != std::string::npos) {
            trimmed = trimmed.substr(arrStart, arrEnd - arrStart + 1);
        }
    }
    
    profiles_.clear();
    
    size_t pos = 0;
    while (pos < trimmed.size()) {
        size_t objStart = trimmed.find('{', pos);
        if (objStart == std::string::npos) break;
        size_t objEnd = trimmed.find('}', objStart);
        if (objEnd == std::string::npos) break;
        
        std::string obj = trimmed.substr(objStart, objEnd - objStart + 1);
        GameProfile p = parseProfile(obj);
        if (!p.game_name.empty()) {
            profiles_.push_back(p);
        }
        pos = objEnd + 1;
    }
    
    save();
    return !profiles_.empty();
}

std::vector<GameProfile> GameProfiles::getAll() const { return profiles_; }

GameProfile GameProfiles::get(const std::string& game_name) const {
    for (const auto& p : profiles_) {
        if (p.game_name == game_name) return p;
    }
    return GameProfile{};
}

bool GameProfiles::has(const std::string& game_name) const {
    for (const auto& p : profiles_) {
        if (p.game_name == game_name) return true;
    }
    return false;
}

void GameProfiles::set(const GameProfile& profile) {
    for (auto& p : profiles_) {
        if (p.game_name == profile.game_name) {
            p = profile;
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
                       [&game_name](const GameProfile& p) { return p.game_name == game_name; }),
        profiles_.end());
    save();
}

GameProfile GameProfiles::detectForProcess(const std::string& process_name) const {
    for (const auto& p : profiles_) {
        if (p.process_name == process_name) return p;
    }
    return GameProfile{};
}

} // namespace gno
