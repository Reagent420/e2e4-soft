#include "game_watcher.h"
#include "game_detector.h"
#include <chrono>

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace gno {

GameWatcher::GameWatcher() = default;

GameWatcher::~GameWatcher() {
    stop();
}

void GameWatcher::start(const GameWatcherConfig& config) {
    if (running_) return;
    
    config_ = config;
    running_ = true;
    known_game_pids_.clear();
    pid_to_game_name_.clear();
    
    // Initial scan
    checkProcesses();
    
    watch_thread_ = std::thread(&GameWatcher::watchLoop, this);
}

void GameWatcher::stop() {
    running_ = false;
    if (watch_thread_.joinable()) {
        watch_thread_.join();
    }
}

bool GameWatcher::isRunning() const {
    return running_;
}

void GameWatcher::setGameStartCallback(GameStartCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    start_callback_ = std::move(callback);
}

void GameWatcher::setGameEndCallback(GameEndCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    end_callback_ = std::move(callback);
}

void GameWatcher::watchLoop() {
    while (running_) {
        checkProcesses();
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.check_interval_ms));
    }
}

void GameWatcher::checkProcesses() {
#ifdef PLATFORM_WINDOWS
    GameDetector detector;
    detector.scanInstalledGames();
    detector.detectRunningGames();
    
    auto running = detector.getRunningGames();
    std::unordered_set<uint32_t> current_pids;
    std::unordered_map<uint32_t, std::string> current_pid_to_game;
    
    // We need to get actual PIDs, so we scan processes directly
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;
    
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    
    if (Process32FirstW(snapshot, &pe)) {
        do {
            for (const auto& game : running) {
                if (_wcsicmp(pe.szExeFile, std::wstring(game.process_name.begin(), game.process_name.end()).c_str()) == 0) {
                    uint32_t pid = pe.th32ProcessID;
                    current_pids.insert(pid);
                    current_pid_to_game[pid] = game.name;
                }
            }
        } while (Process32NextW(snapshot, &pe));
    }
    CloseHandle(snapshot);
    
    // Detect new games
    for (const auto& [pid, game_name] : current_pid_to_game) {
        if (known_game_pids_.find(pid) == known_game_pids_.end()) {
            known_game_pids_.insert(pid);
            pid_to_game_name_[pid] = game_name;
            
            GameStartCallback cb;
            {
                std::lock_guard<std::mutex> lock(callback_mutex_);
                cb = start_callback_;
            }
            if (cb && config_.notify_on_game_start) {
                cb(game_name, "", pid);
            }
        }
    }
    
    // Detect ended games
    std::vector<uint32_t> ended_pids;
    for (const auto& pid : known_game_pids_) {
        if (current_pids.find(pid) == current_pids.end()) {
            ended_pids.push_back(pid);
        }
    }
    
    for (uint32_t pid : ended_pids) {
        auto it = pid_to_game_name_.find(pid);
        std::string game_name = (it != pid_to_game_name_.end()) ? it->second : "Unknown";
        known_game_pids_.erase(pid);
        pid_to_game_name_.erase(pid);
        
        GameEndCallback cb;
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            cb = end_callback_;
        }
        if (cb && config_.notify_on_game_end) {
            cb(game_name, "");
        }
    }
#endif
}

std::vector<std::pair<std::string, uint32_t>> GameWatcher::checkNow() {
    std::vector<std::pair<std::string, uint32_t>> result;
    
#ifdef PLATFORM_WINDOWS
    GameDetector detector;
    detector.scanInstalledGames();
    detector.detectRunningGames();
    
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return result;
    
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    
    if (Process32FirstW(snapshot, &pe)) {
        do {
            for (const auto& game : detector.getRunningGames()) {
                if (_wcsicmp(pe.szExeFile, std::wstring(game.process_name.begin(), game.process_name.end()).c_str()) == 0) {
                    result.emplace_back(game.name, pe.th32ProcessID);
                }
            }
        } while (Process32NextW(snapshot, &pe));
    }
    CloseHandle(snapshot);
#endif
    
    return result;
}

} // namespace gno