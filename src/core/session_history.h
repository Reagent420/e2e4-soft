#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <chrono>
#include <mutex>

namespace gno {

struct SessionRecord {
    std::string game_name;
    std::string start_time_str;
    std::string end_time_str;
    double avg_ping_ms = 0.0;
    double avg_jitter_ms = 0.0;
    double avg_packet_loss = 0.0;
    double max_ping_ms = 0.0;
    uint32_t duration_seconds = 0;
    bool boost_was_active = false;
};

class SessionHistory {
public:
    SessionHistory();
    ~SessionHistory();

    void recordStart(const std::string& game_name, bool boost);
    void recordEnd(double avg_ping, double avg_jitter, double loss, double max_ping);

    std::vector<SessionRecord> getAll() const;
    std::vector<SessionRecord> getLast(int count) const;
    void clear();

    bool saveToFile(const std::string& path = "") const;
    bool loadFromFile(const std::string& path = "");

    std::string getSavePath() const;
    double getAveragePing() const;
    double getAverageJitter() const;

private:
    std::string getAppDataPath() const;
    bool saveToFileUnlocked(const std::string& path) const;

    mutable std::mutex mutex_;
    std::vector<SessionRecord> records_;
    SessionRecord current_;
    bool recording_ = false;
};

} // namespace gno
