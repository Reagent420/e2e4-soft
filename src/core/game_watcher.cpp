#include "game_watcher.h"
#include "game_detector.h"
#include <algorithm>
#include <chrono>

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace gno {

GameWatcher::GameWatcher(SnapshotProvider snapshot_provider)
    : snapshot_provider_(std::move(snapshot_provider)) {}

GameWatcher::~GameWatcher() {
    stop();
}

void GameWatcher::start(const GameWatcherConfig& config) {
    std::thread completed_worker;
    {
        std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
        if (running_ || (watch_thread_.joinable() &&
                         watch_thread_.get_id() == std::this_thread::get_id())) {
            return;
        }
        if (watch_thread_.joinable()) {
            completed_worker = std::move(watch_thread_);
        }
    }
    if (completed_worker.joinable()) {
        completed_worker.join();
    }

    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    if (running_) {
        return;
    }
    config_ = config;
    config_.check_interval_ms = std::clamp(config_.check_interval_ms, 10u, 60000u);
    running_ = true;
    waiting_ = false;
    known_game_pids_.clear();
    pid_to_game_name_.clear();
    watch_thread_ = std::thread(&GameWatcher::watchLoop, this);
}

void GameWatcher::stop() {
    running_ = false;
    waiting_ = false;
    wait_cv_.notify_all();
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    if (watch_thread_.joinable() && watch_thread_.get_id() != std::this_thread::get_id()) {
        watch_thread_.join();
    }
}

bool GameWatcher::isRunning() const {
    return running_;
}

bool GameWatcher::isWaiting() const {
    return waiting_;
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
        if (!running_) {
            break;
        }
        waiting_ = true;
        std::unique_lock<std::mutex> lock(wait_mutex_);
        wait_cv_.wait_for(lock, std::chrono::milliseconds(config_.check_interval_ms), [this] {
            return !running_;
        });
        waiting_ = false;
    }
    waiting_ = false;
}

void GameWatcher::checkProcesses() {
    const auto observed_games = snapshot_provider_ ? snapshot_provider_() : collectObservedGames();
    if (!running_) {
        return;
    }
    std::unordered_set<uint32_t> current_pids;
    std::unordered_map<uint32_t, ObservedGame> current_games;
    for (const auto& game : observed_games) {
        if (!running_) {
            return;
        }
        current_pids.insert(game.pid);
        current_games.emplace(game.pid, game);
    }

    for (const auto& [pid, game] : current_games) {
        if (!running_) {
            return;
        }
        if (known_game_pids_.find(pid) == known_game_pids_.end()) {
            known_game_pids_.insert(pid);
            pid_to_game_name_[pid] = game.game_name;

            GameStartCallback callback;
            {
                std::lock_guard<std::mutex> lock(callback_mutex_);
                callback = start_callback_;
            }
            if (callback && config_.notify_on_game_start) {
                callback(game.game_name, game.process_name, pid);
            }
        }
    }

    std::vector<uint32_t> ended_pids;
    for (const auto pid : known_game_pids_) {
        if (current_pids.find(pid) == current_pids.end()) {
            ended_pids.push_back(pid);
        }
    }
    for (const auto pid : ended_pids) {
        if (!running_) {
            return;
        }
        const auto it = pid_to_game_name_.find(pid);
        const auto game_name = it != pid_to_game_name_.end() ? it->second : "Unknown";
        known_game_pids_.erase(pid);
        pid_to_game_name_.erase(pid);

        GameEndCallback callback;
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            callback = end_callback_;
        }
        if (callback && config_.notify_on_game_end) {
            callback(game_name, "");
        }
    }
}

std::vector<GameWatcher::ObservedGame> GameWatcher::collectObservedGames() const {
    std::vector<ObservedGame> observed_games;
#ifdef PLATFORM_WINDOWS
    GameDetector detector;
    detector.scanInstalledGames();
    if (!running_) return observed_games;
    detector.detectRunningGames();
    if (!running_) return observed_games;
    const auto running_games = detector.getRunningGames();

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return observed_games;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snapshot, &pe)) {
        do {
            if (!running_) break;
            for (const auto& game : running_games) {
                if (_wcsicmp(pe.szExeFile, std::wstring(game.process_name.begin(), game.process_name.end()).c_str()) == 0) {
                    observed_games.push_back({game.name, game.process_name, pe.th32ProcessID});
                }
            }
        } while (Process32NextW(snapshot, &pe));
    }
    CloseHandle(snapshot);
#endif
    return observed_games;
}

std::vector<std::pair<std::string, uint32_t>> GameWatcher::checkNow() {
    std::vector<std::pair<std::string, uint32_t>> result;
    for (const auto& game : snapshot_provider_ ? snapshot_provider_() : collectObservedGames()) {
        if (!running_ && !snapshot_provider_) {
            break;
        }
        result.emplace_back(game.game_name, game.pid);
    }
    return result;
}

} // namespace gno
