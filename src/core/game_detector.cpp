#include "game_detector.h"
#include <fstream>
#include <sstream>
#include <algorithm>

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <cstdlib>
#include <cwchar>
#pragma comment(lib, "psapi.lib")
#endif

namespace gno {

static std::wstring toWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring ws(len - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &ws[0], len);
    return ws;
}

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
        {"RocketLeague.exe", "RocketLeague.exe", "", {7000, 7100}, {"23.52.0.0/16"}, "Sports", ""},
        {"Dead by Daylight", "DeadByDaylight.exe", "", {7777, 7777}, {"23.52.0.0/16"}, "Horror", ""},
        {"Rust", "RustClient.exe", "", {28015, 28016}, {"162.244.52.0/24"}, "Survival", ""},
        {"Minecraft", "javaw.exe", "", {25565, 25565}, {"52.0.0.0/8"}, "Sandbox", ""},
        {"Escape from Tarkov", "EscapeFromTarkov.exe", "", {10000, 10000}, {"37.230.0.0/16"}, "FPS", ""},
        {"Brawlhalla", "BrawlhallaGame.exe", "", {7000, 7100}, {"23.52.0.0/16"}, "Fighting", ""},
        {"Fall Guys", "FallGuys_client_game.exe", "", {7000, 7100}, {"23.52.0.0/16"}, "Party", ""}
    };
}

GameDetector::~GameDetector() = default;

void GameDetector::loadGameDatabase(const std::string& json_path) {
    std::ifstream file(json_path);
    if (!file.is_open()) return;
    
    std::stringstream buffer;
    buffer << file.rdbuf();
}

void GameDetector::scanInstalledGames() {
    installed_games_.clear();
    
    for (auto& game : supported_games_) {
        game.is_installed = !findExecutablePath(game.process_name).empty();
        game.executable_path = findExecutablePath(game.process_name);
        if (game.is_installed) {
            installed_games_.push_back(game);
        }
    }
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

} // namespace gno
