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
    cancellation_source_ = CancellationSource{};
    const auto cancellation = cancellation_source_.token();
    watch_thread_ = std::thread([this, cancellation] { watchLoop(cancellation); });
}

void GameWatcher::stop() {
    std::thread completed_worker;
    CancellationSource cancellation;
    {
        std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
        running_ = false;
        waiting_ = false;
        cancellation = cancellation_source_;
        if (watch_thread_.joinable() && watch_thread_.get_id() != std::this_thread::get_id()) {
            completed_worker = std::move(watch_thread_);
        }
    }
    cancellation.cancel();
    wait_cv_.notify_all();
    if (completed_worker.joinable()) {
        completed_worker.join();
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

void GameWatcher::watchLoop(const CancellationToken& cancellation) {
    while (running_) {
        checkProcesses(cancellation);
        if (!running_ || cancellation.isCancelled()) {
            break;
        }
        waiting_ = true;
        std::unique_lock<std::mutex> lock(wait_mutex_);
        wait_cv_.wait_for(lock, std::chrono::milliseconds(config_.check_interval_ms), [this, &cancellation] {
            return !running_ || cancellation.isCancelled();
        });
        waiting_ = false;
    }
    waiting_ = false;
}

void GameWatcher::checkProcesses(const CancellationToken& cancellation) {
    const auto observed_games = snapshot_provider_
        ? snapshot_provider_(cancellation)
        : collectObservedGames(cancellation);
    if (!running_ || cancellation.isCancelled()) {
        return;
    }
    std::unordered_set<uint32_t> current_pids;
    std::unordered_map<uint32_t, ObservedGame> current_games;
    for (const auto& game : observed_games) {
        if (!running_ || cancellation.isCancelled()) {
            return;
        }
        current_pids.insert(game.pid);
        current_games.emplace(game.pid, game);
    }

    for (const auto& [pid, game] : current_games) {
        if (!running_ || cancellation.isCancelled()) {
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
        if (!running_ || cancellation.isCancelled()) {
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

std::vector<GameWatcher::ObservedGame> GameWatcher::collectObservedGames(
    const CancellationToken& cancellation) const {
    std::vector<ObservedGame> observed_games;
#ifndef PLATFORM_WINDOWS
    (void)cancellation;
#endif
#ifdef PLATFORM_WINDOWS
    GameDetector detector;
    detector.detectRunningGames();
    if (cancellation.isCancelled()) return observed_games;
    const auto running_games = detector.getRunningGames();

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return observed_games;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snapshot, &pe)) {
        do {
            if (cancellation.isCancelled()) break;
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
    const CancellationToken cancellation;
    const auto observed_games = snapshot_provider_
        ? snapshot_provider_(cancellation)
        : collectObservedGames(cancellation);
    for (const auto& game : observed_games) {
        result.emplace_back(game.game_name, game.pid);
    }
    return result;
}

} // namespace gno
