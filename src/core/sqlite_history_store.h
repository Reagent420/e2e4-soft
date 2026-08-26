#pragma once

// SQLite-backed session history storage (v2.6).
// Replaces JSON file storage with structured queries.
// Schema is auto-created on first open.

#include <string>
#include <vector>

struct sqlite3;

namespace gno {

struct HistoryRecord {
    std::string game_name;
    std::string start_time_str;
    std::string end_time_str;
    double avg_ping_ms = 0.0;
    double avg_jitter_ms = 0.0;
    double avg_packet_loss = 0.0;
    double max_ping_ms = 0.0;
    double quality_score = 0.0;
    bool boost_was_active = false;
    int duration_seconds = 0;
};

class SqliteHistoryStore {
public:
    explicit SqliteHistoryStore(const std::string& db_path);
    ~SqliteHistoryStore();

    // Non-copyable
    SqliteHistoryStore(const SqliteHistoryStore&) = delete;
    SqliteHistoryStore& operator=(const SqliteHistoryStore&) = delete;

    void insert(const HistoryRecord& rec);
    std::vector<HistoryRecord> getLast(int count) const;
    double averageScore() const;
    int totalSessions() const;

private:
    sqlite3* db_ = nullptr;
    void createSchema();
};

} // namespace gno
