#include "game_profiles.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#include <shlobj.h>
#endif

namespace gno {

static std::string getAppDataPath() {
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

    std::string line;
    GameProfile current;
    bool inProfile = false;

    while (std::getline(file, line)) {
        auto trim = [](std::string& s) {
            s.erase(0, s.find_first_not_of(" \t\r\n{"));
            s.erase(s.find_last_not_of(" \t\r\n},") + 1);
        };

        if (line.find("\"game_name\"") != std::string::npos) {
            if (inProfile && !current.game_name.empty()) {
                profiles_.push_back(current);
            }
            current = GameProfile{};
            inProfile = true;
            auto pos = line.find(':');
            if (pos != std::string::npos) {
                current.game_name = line.substr(pos + 1);
                trim(current.game_name);
                current.game_name.erase(
                    std::remove(current.game_name.begin(), current.game_name.end(), '"'),
                    current.game_name.end());
            }
        } else if (line.find("\"process_name\"") != std::string::npos) {
            auto pos = line.find(':');
            if (pos != std::string::npos) {
                current.process_name = line.substr(pos + 1);
                trim(current.process_name);
                current.process_name.erase(
                    std::remove(current.process_name.begin(), current.process_name.end(), '"'),
                    current.process_name.end());
            }
        } else if (line.find("\"multipath_enabled\"") != std::string::npos) {
            current.multipath_enabled = line.find("true") != std::string::npos;
        } else if (line.find("\"fps_boost_enabled\"") != std::string::npos) {
            current.fps_boost_enabled = line.find("true") != std::string::npos;
        } else if (line.find("\"network_optimization\"") != std::string::npos) {
            current.network_optimization = line.find("true") != std::string::npos;
        } else if (line.find("\"max_routes\"") != std::string::npos) {
            auto pos = line.find(':');
            if (pos != std::string::npos) {
                current.max_routes = std::stoi(line.substr(pos + 1));
            }
        } else if (line.find("\"auto_apply\"") != std::string::npos) {
            current.auto_apply = line.find("true") != std::string::npos;
        }
    }

    if (inProfile && !current.game_name.empty()) {
        profiles_.push_back(current);
    }
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
        file << "    \"game_name\": \"" << p.game_name << "\",\n";
        file << "    \"process_name\": \"" << p.process_name << "\",\n";
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
