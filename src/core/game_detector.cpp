#include "game_detector.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <map>
#include <regex>

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <cstdlib>
#include <cwchar>
#include <shlobj.h>
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "shell32.lib")
#endif

namespace gno {

#ifdef PLATFORM_WINDOWS
static std::wstring toWide(const std::string& value) {
    if (value.empty()) return {};
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) return {};

    std::wstring result(static_cast<std::size_t>(required), L'\0');
    const int written = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), required);
    if (written != required) return {};
    return result;
}
#endif

GameDetector::GameDetector() {
    supported_games_ = {
        {"Counter-Strike 2", "cs2.exe", "", {27015, 27016}, {"155.133.226.0/24"}, "FPS", ""},
        {"Dota 2", "dota2.exe", "", {27015, 27016}, {"155.133.226.0/24"}, "MOBA", ""},
        {"VALORANT", "VALORANT-Win64-Shipping.exe", "", {7000, 7000}, {"162.249.72.0/24"}, "FPS", ""},
        {"Fortnite", "FortniteClient-Win64-Shipping.exe", "", {5222, 5222}, {"154.59.128.0/24"}, "Battle Royale", ""},
        {"Apex Legends", "r5apex.exe", "", {37000, 40000}, {"159.153.0.0/16"}, "Battle Royale", ""},
        {"League of Legends", "League of Legends.exe", "", {5000, 5500}, {"104.160.0.0/12"}, "MOBA", ""},
        {"Overwatch 2", "Overwatch.exe", "", {1119, 1120}, {"24.105.0.0/16"}, "FPS", ""},
        {"PUBG", "TslGame.exe", "", {7000, 7100}, {"23.52.0.0/16"}, "Battle Royale", ""},
        {"Rainbow Six Siege", "RainbowSix.exe", "", {37000, 40000}, {"23.52.0.0/16"}, "FPS", ""},
        {"Call of Duty: Warzone", "ModernWarfare.exe", "", {3074, 3074}, {"24.105.0.0/16"}, "FPS", ""},
        {"Genshin Impact", "GenshinImpact.exe", "", {443, 443}, {"47.246.0.0/16"}, "RPG", ""},
        {"World of Warcraft", "Wow.exe", "", {3724, 3724}, {"195.12.0.0/16"}, "MMORPG", ""},
        {"EVE Online", "exefile.exe", "", {26000, 26004}, {"87.237.0.0/16"}, "MMORPG", ""},
        {"Rocket League", "RocketLeague.exe", "", {7000, 7100}, {"23.52.0.0/16"}, "Sports", ""},
        {"Dead by Daylight", "DeadByDaylight.exe", "", {7777, 7777}, {"23.52.0.0/16"}, "Horror", ""},
        {"Rust", "RustClient.exe", "", {28015, 28016}, {"162.244.52.0/24"}, "Survival", ""},
        {"Minecraft", "javaw.exe", "", {25565, 25565}, {"52.0.0.0/8"}, "Sandbox", ""},
        {"Escape from Tarkov", "EscapeFromTarkov.exe", "", {10000, 10000}, {"37.230.0.0/16"}, "FPS", ""},
        {"Brawlhalla", "BrawlhallaGame.exe", "", {7000, 7100}, {"23.52.0.0/16"}, "Fighting", ""},
        {"Fall Guys", "FallGuys_client_game.exe", "", {7000, 7100}, {"23.52.0.0/16"}, "Party", ""},
        {"Left 4 Dead 2", "left4dead2.exe", "", {27015, 27016}, {"155.133.226.0/24"}, "FPS", ""},
        {"Killing Floor 2", "KFGame.exe", "", {7777, 7777}, {"23.52.0.0/16"}, "FPS", ""},
        {"Poppy Playtime", "PlaytimeLauncher.exe", "", {27015, 27016}, {"155.133.226.0/24"}, "Horror", ""}
    };
}

GameDetector::~GameDetector() = default;

void GameDetector::loadGameDatabase(const std::string& json_path) {
    std::ifstream file(json_path);
    if (!file.is_open()) return;
    
    std::stringstream buffer;
    buffer << file.rdbuf();
}

void GameDetector::detectRunningGames() {
    running_games_.clear();
    
#ifdef PLATFORM_WINDOWS
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;
    
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    
    if (Process32FirstW(snapshot, &pe)) {
        do {
            for (auto& game : supported_games_) {
                if (_wcsicmp(pe.szExeFile, toWide(game.process_name).c_str()) == 0) {
                    game.is_running = true;
                    running_games_.push_back(game);
                }
            }
        } while (Process32NextW(snapshot, &pe));
    }
    
    CloseHandle(snapshot);
#endif
}

std::vector<GameInfo> GameDetector::getSupportedGames() const {
    return supported_games_;
}

std::vector<GameInfo> GameDetector::getInstalledGames() const {
    return installed_games_;
}

std::vector<GameInfo> GameDetector::getRunningGames() const {
    return running_games_;
}

std::optional<GameInfo> GameDetector::getGameByName(const std::string& name) const {
    for (const auto& game : supported_games_) {
        if (game.name == name) {
            return game;
        }
    }
    return std::nullopt;
}

std::optional<GameInfo> GameDetector::getGameByProcess(const std::string& process_name) const {
    for (const auto& game : supported_games_) {
        if (game.process_name == process_name) {
            return game;
        }
    }
    return std::nullopt;
}

std::optional<GameInfo> GameDetector::findGameForConnection(const std::string& dest_ip, uint16_t dest_port) const {
    for (const auto& game : supported_games_) {
        for (uint16_t port : game.game_ports) {
            if (port == dest_port) {
                return game;
            }
        }
    }
    return std::nullopt;
}

std::vector<GameRegion> GameDetector::getRegionsForGame(const std::string& game_name) const {
    std::vector<GameRegion> regions;
    
    GameRegion europe;
    europe.name = "europe";
    europe.display_name = "Europe";
    europe.server_ips = {"89.248.165.0/24", "185.50.104.0/24"};
    europe.recommended_gateways = {"89.248.165.1"};
    regions.push_back(europe);
    
    GameRegion asia;
    asia.name = "asia";
    asia.display_name = "Asia";
    asia.server_ips = {"103.28.54.0/24", "43.154.0.0/16"};
    asia.recommended_gateways = {"103.28.54.1"};
    regions.push_back(asia);
    
    GameRegion na;
    na.name = "na";
    na.display_name = "North America";
    na.server_ips = {"192.64.170.0/24", "162.244.52.0/24"};
    na.recommended_gateways = {"192.64.170.1"};
    regions.push_back(na);
    
    GameRegion sa;
    sa.name = "sa";
    sa.display_name = "South America";
    sa.server_ips = {"205.196.6.0/24"};
    sa.recommended_gateways = {"205.196.6.1"};
    regions.push_back(sa);
    
    GameRegion ru;
    ru.name = "ru";
    ru.display_name = "Russia/CIS";
    ru.server_ips = {"185.50.104.0/24", "89.248.165.0/24"};
    ru.recommended_gateways = {"185.50.104.1"};
    regions.push_back(ru);
    
    return regions;
}

void GameDetector::addCustomGame(const GameInfo& game) {
    supported_games_.push_back(game);
}

void GameDetector::removeCustomGame(const std::string& game_name) {
    supported_games_.erase(
        std::remove_if(supported_games_.begin(), supported_games_.end(),
                       [&game_name](const GameInfo& g) { return g.name == game_name; }),
        supported_games_.end());
}

bool GameDetector::isProcessRunning(const std::string& process_name) const {
#ifdef PLATFORM_WINDOWS
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    
    bool found = false;
    if (Process32FirstW(snapshot, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, toWide(process_name).c_str()) == 0) {
                found = true;
                break;
            }
        } while (Process32NextW(snapshot, &pe));
    }
    
    CloseHandle(snapshot);
    return found;
#else
    return false;
#endif
}

std::string GameDetector::findExecutablePath(const std::string& process_name) const {
#ifdef PLATFORM_WINDOWS
    char path[MAX_PATH] = {0};
    if (SearchPathA(nullptr, process_name.c_str(), nullptr, MAX_PATH, path, nullptr)) {
        return path;
    }
#endif
    return "";
}

// ===== Steam/Epic/GOG Detection =====

std::string GameDetector::getSteamPath() const {
#ifdef PLATFORM_WINDOWS
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char path[MAX_PATH] = {0};
        DWORD size = MAX_PATH;
        if (RegQueryValueExA(hKey, "SteamPath", nullptr, nullptr, (LPBYTE)path, &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return std::string(path);
        }
        RegCloseKey(hKey);
    }
    char programFiles[MAX_PATH];
    if (SHGetFolderPathA(nullptr, CSIDL_PROGRAM_FILESX86, nullptr, 0, programFiles) == S_OK) {
        return std::string(programFiles) + "\\Steam";
    }
#endif
    return "";
}

std::string GameDetector::getEpicPath() const {
#ifdef PLATFORM_WINDOWS
    char programFiles[MAX_PATH];
    if (SHGetFolderPathA(nullptr, CSIDL_PROGRAM_FILESX86, nullptr, 0, programFiles) == S_OK) {
        return std::string(programFiles) + "\\Epic Games";
    }
#endif
    return "";
}

std::string GameDetector::getGOGPath() const {
#ifdef PLATFORM_WINDOWS
    char programFiles[MAX_PATH];
    if (SHGetFolderPathA(nullptr, CSIDL_PROGRAM_FILESX86, nullptr, 0, programFiles) == S_OK) {
        return std::string(programFiles) + "\\GOG Galaxy";
    }
#endif
    return "";
}

std::vector<std::string> GameDetector::getSteamLibraryFolders() const {
    std::vector<std::string> folders;
#ifdef PLATFORM_WINDOWS
    std::string steamPath = getSteamPath();
    if (steamPath.empty()) return folders;
    
    folders.push_back(steamPath + "\\steamapps");
    
    std::string vdfPath = steamPath + "\\steamapps\\libraryfolders.vdf";
    std::ifstream file(vdfPath);
    if (file.is_open()) {
        std::string line;
        std::regex pathRegex(R"rx("path"\s*"([^"]+)")rx");
        while (std::getline(file, line)) {
            std::smatch match;
            if (std::regex_search(line, match, pathRegex)) {
                std::string path = match[1].str();
                std::replace(path.begin(), path.end(), '/', '\\');
                if (!path.empty() && path.back() != '\\') path += "\\";
                folders.push_back(path + "steamapps");
            }
        }
    }
#endif
    return folders;
}

std::string GameDetector::findExecutableInDir(const std::string& dir, const std::string& process_name) const {
    if (!std::filesystem::exists(dir)) return "";
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().filename().string() == process_name) {
            std::string path = entry.path().string();
            // normalize: forward slashes and duplicate backslashes
            std::replace(path.begin(), path.end(), '/', '\\');
            std::string doubleSep = "\\\\";
            size_t pos = 0;
            while ((pos = path.find(doubleSep, pos)) != std::string::npos) {
                path.replace(pos, 2, "\\");
            }
            return path;
        }
    }
    return "";
}

void GameDetector::scanSteamLibrary() {
#ifdef PLATFORM_WINDOWS
    auto folders = getSteamLibraryFolders();
    if (folders.empty()) return;
    
    std::map<std::string, GameInfo*> appIdMap;
    for (auto& game : supported_games_) {
        if (game.name == "Counter-Strike 2") appIdMap["730"] = &game;
        else if (game.name == "Dota 2") appIdMap["570"] = &game;
        else if (game.name == "PUBG") appIdMap["578080"] = &game;
        else if (game.name == "Apex Legends") appIdMap["1172470"] = &game;
        else if (game.name == "Rust") appIdMap["252490"] = &game;
        else if (game.name == "Left 4 Dead 2") appIdMap["550"] = &game;
        else if (game.name == "Killing Floor 2") appIdMap["232090"] = &game;
        else if (game.name == "Poppy Playtime") appIdMap["1721470"] = &game;
    }
    
    for (const auto& folder : folders) {
        std::string acfDir = folder + "\\appmanifest_*.acf";
        WIN32_FIND_DATAA findData;
        HANDLE hFind = FindFirstFileA(acfDir.c_str(), &findData);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (findData.cFileName[0] != '.') {
                    std::string acfPath = folder + "\\" + findData.cFileName;
                    std::ifstream acfFile(acfPath);
                    std::string content((std::istreambuf_iterator<char>(acfFile)), std::istreambuf_iterator<char>());
                    
                    std::smatch match;
                    std::string appid, installdir;
                    std::regex appidRegex(R"rx("appid"\s+"(\d+)")rx");
                    std::regex installdirRegex(R"rx("installdir"\s+"([^"]+)")rx");
                    
                    if (std::regex_search(content, match, appidRegex)) appid = match[1].str();
                    if (std::regex_search(content, match, installdirRegex)) installdir = match[1].str();
                    
                    if (!appid.empty() && appIdMap.count(appid) && appIdMap[appid]) {
                        GameInfo* game = appIdMap[appid];
                        std::string installPath = folder + "\\common\\" + installdir;
                        game->executable_path = findExecutableInDir(installPath, game->process_name);
                        if (!game->executable_path.empty()) {
                            game->is_installed = true;
                        }
                    }
                }
            } while (FindNextFileA(hFind, &findData));
            FindClose(hFind);
        }
    }
    
    installed_games_.clear();
    for (auto& game : supported_games_) {
        if (game.is_installed) installed_games_.push_back(game);
    }
#endif
}

void GameDetector::scanEpicLibrary() {
#ifdef PLATFORM_WINDOWS
    char localAppData[MAX_PATH];
    if (SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localAppData) != S_OK) return;
    
    std::string manifestDir = std::string(localAppData) + "\\EpicGamesLauncher\\Saved\\Manifests";
    if (!std::filesystem::exists(manifestDir)) return;
    
    for (auto& game : supported_games_) {
        std::string manifestPattern;
        if (game.name == "Fortnite") manifestPattern = "Fortnite";
        else if (game.name == "Rocket League") manifestPattern = "RocketLeague";
        
        if (manifestPattern.empty()) continue;
        
        for (const auto& entry : std::filesystem::directory_iterator(manifestDir)) {
            if (entry.path().extension() == ".item") {
                std::ifstream f(entry.path());
                std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                if (content.find(manifestPattern) != std::string::npos && content.find("InstallLocation") != std::string::npos) {
                    std::smatch match;
                    std::regex locRegex(R"rx("InstallLocation"\s*:\s*"([^"]+)")rx");
                    if (std::regex_search(content, match, locRegex)) {
                        std::string installPath = match[1].str();
                        std::replace(installPath.begin(), installPath.end(), '/', '\\');
                        game.executable_path = findExecutableInDir(installPath, game.process_name);
                        if (!game.executable_path.empty()) game.is_installed = true;
                    }
                }
            }
        }
    }
    
    installed_games_.clear();
    for (auto& game : supported_games_) {
        if (game.is_installed) installed_games_.push_back(game);
    }
#endif
}

void GameDetector::scanGOGLibrary() {
#ifdef PLATFORM_WINDOWS
    char localAppData[MAX_PATH];
    if (SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localAppData) != S_OK) return;
    
    std::string gogDir = std::string(localAppData) + "\\GOG.com\\Galaxy\\storage";
    if (!std::filesystem::exists(gogDir)) return;
    
    for (auto& game : supported_games_) {
        if (game.name == "The Witcher 3" || game.name == "Cyberpunk 2077") {
            for (const auto& entry : std::filesystem::directory_iterator(gogDir)) {
                if (entry.path().extension() == ".json") {
                    std::ifstream f(entry.path());
                    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                    if (content.find(game.name) != std::string::npos) {
                        std::smatch match;
                        std::regex pathRegex(R"rx("installation_path"\s*:\s*"([^"]+)")rx");
                        if (std::regex_search(content, match, pathRegex)) {
                            std::string installPath = match[1].str();
                            std::replace(installPath.begin(), installPath.end(), '/', '\\');
                            game.executable_path = findExecutableInDir(installPath, game.process_name);
                            if (!game.executable_path.empty()) game.is_installed = true;
                        }
                    }
                }
            }
        }
    }
    
    installed_games_.clear();
    for (auto& game : supported_games_) {
        if (game.is_installed) installed_games_.push_back(game);
    }
#endif
}

std::vector<GameInstallInfo> GameDetector::findGameInstallations(const std::string& game_name) {
    std::vector<GameInstallInfo> results;
    
    for (const auto& game : supported_games_) {
        if (game.name != game_name) continue;
        
        if (game.is_installed && !game.executable_path.empty()) {
            GameInstallInfo info;
            info.game_name = game.name;
            info.executable_path = game.executable_path;
            info.store = GameStore::Steam;
            info.install_dir = std::filesystem::path(game.executable_path).parent_path().string();
            results.push_back(info);
        }
    }
    
    return results;
}

void GameDetector::scanInstalledGames() {
    scanSteamLibrary();
    scanEpicLibrary();
    scanGOGLibrary();
    
    for (auto& game : supported_games_) {
        if (!game.is_installed) {
            game.executable_path = findExecutablePath(game.process_name);
            if (!game.executable_path.empty()) {
                game.is_installed = true;
            }
        }
    }
    
    installed_games_.clear();
    for (auto& game : supported_games_) {
        if (game.is_installed) {
            installed_games_.push_back(game);
        }
    }
}

} // namespace gno
