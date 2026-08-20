#pragma once

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <unordered_set>

namespace gno {

struct GameWatcherConfig {
    bool enabled = true;
    uint32_t check_interval_ms = 2000;
    bool auto_apply_profiles = false;
    bool notify_on_game_start = true;
    bool notify_on_game_end = true;
};

class GameWatcher {
public:
    struct ObservedGame {
        std::string game_name;
        std::string process_name;
        uint32_t pid = 0;
    };
    using SnapshotProvider = std::function<std::vector<ObservedGame>()>;

    explicit GameWatcher(SnapshotProvider snapshot_provider = {});
    ~GameWatcher();

    void start(const GameWatcherConfig& config = {});
    void stop();
    bool isRunning() const;
    bool isWaiting() const;

    using GameStartCallback = std::function<void(const std::string& game_name, const std::string& process_name, uint32_t pid)>;
    using GameEndCallback = std::function<void(const std::string& game_name, const std::string& process_name)>;

    void setGameStartCallback(GameStartCallback callback);
    void setGameEndCallback(GameEndCallback callback);

    // Manual check - returns list of newly detected running games
    std::vector<std::pair<std::string, uint32_t>> checkNow();

private:
    void watchLoop();
    void checkProcesses();
    std::vector<ObservedGame> collectObservedGames() const;

    GameWatcherConfig config_;
    std::atomic<bool> running_{false};
    std::atomic<bool> waiting_{false};
    std::thread watch_thread_;
    std::mutex lifecycle_mutex_;
    std::mutex wait_mutex_;
    std::condition_variable wait_cv_;
    mutable std::mutex callback_mutex_;
    
    GameStartCallback start_callback_;
    GameEndCallback end_callback_;
    SnapshotProvider snapshot_provider_;
    
    std::unordered_set<uint32_t> known_game_pids_;
    std::unordered_map<uint32_t, std::string> pid_to_game_name_;
};

} // namespace gno
