#include "game_watcher.h"
#include "game_detector.h"
#include <algorithm>
#include <chrono>
#include <cctype>

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace gno {

namespace {

bool hasSameProcessName(const std::string& left, const std::string& right) {
    return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin(),
        [](const char left_char, const char right_char) {
            return std::tolower(static_cast<unsigned char>(left_char)) ==
                   std::tolower(static_cast<unsigned char>(right_char));
        });
}

} // namespace

GameWatcher::GameWatcher() = default;

GameWatcher::GameWatcher(SnapshotProvider snapshot_provider)
    : snapshot_provider_(std::move(snapshot_provider)) {}

GameWatcher::GameWatcher(ProcessProvider process_provider)
    : process_provider_(std::move(process_provider)) {}

GameWatcher::~GameWatcher() {
    stop();
}

void GameWatcher::start(const GameWatcherConfig& config) {
    std::thread completed_worker;
    {
        std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
        if (lifecycle_state_ != LifecycleState::Stopped ||
            (watch_thread_.joinable() && watch_thread_.get_id() == std::this_thread::get_id())) {
            return;
        }
        if (watch_thread_.joinable()) {
            lifecycle_state_ = LifecycleState::Stopping;
            completed_worker = std::move(watch_thread_);
        }
    }
    if (completed_worker.joinable()) {
        completed_worker.join();
        std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
        if (lifecycle_state_ == LifecycleState::Stopping) {
            lifecycle_state_ = LifecycleState::Stopped;
            lifecycle_cv_.notify_all();
        }
    }

    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    if (lifecycle_state_ != LifecycleState::Stopped) {
        return;
    }
    config_ = config;
    config_.check_interval_ms = std::clamp(config_.check_interval_ms, 10u, 60000u);
    waiting_ = false;
    known_game_pids_.clear();
    pid_to_game_name_.clear();
    cancellation_source_ = CancellationSource{};
    const auto cancellation = cancellation_source_.token();
    lifecycle_state_ = LifecycleState::Running;
    watch_thread_ = std::thread([this, cancellation] { watchLoop(cancellation); });
}

void GameWatcher::stop() {
    std::thread completed_worker;
    CancellationSource cancellation;
    {
        std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
        if (lifecycle_state_ == LifecycleState::Stopped) {
            if (watch_thread_.joinable() && watch_thread_.get_id() != std::this_thread::get_id()) {
                completed_worker = std::move(watch_thread_);
            }
        } else {
            lifecycle_state_ = LifecycleState::Stopping;
            cancellation = cancellation_source_;
        }
        waiting_ = false;
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
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    return lifecycle_state_ == LifecycleState::Running;
}

bool GameWatcher::isStopping() const {
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    return lifecycle_state_ == LifecycleState::Stopping;
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
    while (!cancellation.isCancelled()) {
        checkProcesses(cancellation);
        if (cancellation.isCancelled()) {
            break;
        }
        waiting_ = true;
        std::unique_lock<std::mutex> lock(wait_mutex_);
        wait_cv_.wait_for(lock, std::chrono::milliseconds(config_.check_interval_ms), [&cancellation] {
            return cancellation.isCancelled();
        });
        waiting_ = false;
    }
    waiting_ = false;
    finishWorker();
}

void GameWatcher::finishWorker() {
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    if (lifecycle_state_ == LifecycleState::Stopping) {
        lifecycle_state_ = LifecycleState::Stopped;
        lifecycle_cv_.notify_all();
    }
}

void GameWatcher::checkProcesses(const CancellationToken& cancellation) {
    const auto observed_games = snapshot_provider_
        ? snapshot_provider_(cancellation)
        : collectObservedGames(cancellation);
    if (cancellation.isCancelled()) {
        return;
    }
    std::unordered_set<uint32_t> current_pids;
    std::unordered_map<uint32_t, ObservedGame> current_games;
    for (const auto& game : observed_games) {
        if (cancellation.isCancelled()) {
            return;
        }
        current_pids.insert(game.pid);
        current_games.emplace(game.pid, game);
    }

    for (const auto& [pid, game] : current_games) {
        if (cancellation.isCancelled()) {
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
            if (cancellation.isCancelled()) {
                return;
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
        if (cancellation.isCancelled()) {
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
    if (process_provider_) {
        return matchObservedProcesses(process_provider_(cancellation), cancellation);
    }
#ifdef PLATFORM_WINDOWS
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return observed_games;

    std::vector<ObservedProcess> processes;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snapshot, &pe)) {
        do {
            if (cancellation.isCancelled()) break;
            const std::wstring process_name(pe.szExeFile);
            processes.push_back({std::string(process_name.begin(), process_name.end()), pe.th32ProcessID});
        } while (Process32NextW(snapshot, &pe));
    }
    CloseHandle(snapshot);
    return matchObservedProcesses(processes, cancellation);
#else
    (void)cancellation;
#endif
    return observed_games;
}

std::vector<GameWatcher::ObservedGame> GameWatcher::matchObservedProcesses(
    const std::vector<ObservedProcess>& processes, const CancellationToken& cancellation) const {
    std::vector<ObservedGame> observed_games;
    if (cancellation.isCancelled()) {
        return observed_games;
    }
    const GameDetector detector;
    const auto supported_games = detector.getSupportedGames();
    for (const auto& process : processes) {
        if (cancellation.isCancelled()) {
            return {};
        }
        for (const auto& game : supported_games) {
            if (cancellation.isCancelled()) {
                return {};
            }
            if (hasSameProcessName(process.process_name, game.process_name)) {
                observed_games.push_back({game.name, game.process_name, process.pid});
                break;
            }
        }
    }
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
