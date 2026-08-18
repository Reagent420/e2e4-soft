#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace gno {

struct GameInfo {
    std::string name;
    std::string process_name;
    std::string executable_path;
    std::vector<uint16_t> game_ports;
    std::vector<std::string> server_ips;
    std::string category;
    std::string icon_path;
    bool is_installed = false;
    bool is_running = false;
};

struct GameRegion {
    std::string name;
    std::string display_name;
    std::vector<std::string> server_ips;
    std::vector<std::string> recommended_gateways;
};

class GameDetector {
public:
    GameDetector();
    ~GameDetector();

    void loadGameDatabase(const std::string& json_path);
    void scanInstalledGames();
    void detectRunningGames();

    std::vector<GameInfo> getSupportedGames() const;
    std::vector<GameInfo> getInstalledGames() const;
    std::vector<GameInfo> getRunningGames() const;
    
    std::optional<GameInfo> getGameByName(const std::string& name) const;
    std::optional<GameInfo> getGameByProcess(const std::string& process_name) const;
    std::optional<GameInfo> findGameForConnection(const std::string& dest_ip, uint16_t dest_port) const;

    std::vector<GameRegion> getRegionsForGame(const std::string& game_name) const;

    void addCustomGame(const GameInfo& game);
    void removeCustomGame(const std::string& game_name);

private:
    bool isProcessRunning(const std::string& process_name) const;
    std::string findExecutablePath(const std::string& process_name) const;
    
    std::vector<GameInfo> supported_games_;
    std::vector<GameInfo> installed_games_;
    std::vector<GameInfo> running_games_;
};

} // namespace gno
