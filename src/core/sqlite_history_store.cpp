#include "core/sqlite_history_store.h"

#include <sqlite3.h>

namespace gno {

SqliteHistoryStore::SqliteHistoryStore(const std::string& db_path) {
    sqlite3_open_v2(db_path.c_str(), &db_,
                    SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (db_) createSchema();
}

SqliteHistoryStore::~SqliteHistoryStore() {
    if (db_) sqlite3_close(db_);
}

void SqliteHistoryStore::createSchema() {
    if (!db_) return;
    const char* sql =
        "CREATE TABLE IF NOT EXISTS sessions ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  game_name TEXT NOT NULL,"
        "  start_time TEXT,"
        "  end_time TEXT,"
        "  avg_ping_ms REAL DEFAULT 0,"
        "  avg_jitter_ms REAL DEFAULT 0,"
        "  avg_packet_loss REAL DEFAULT 0,"
        "  max_ping_ms REAL DEFAULT 0,"
        "  quality_score REAL DEFAULT 0,"
        "  boost_was_active INTEGER DEFAULT 0,"
        "  duration_seconds INTEGER DEFAULT 0"
        ");";
    char* err = nullptr;
    sqlite3_exec(db_, sql, nullptr, nullptr, &err);
    if (err) sqlite3_free(err);
}

void SqliteHistoryStore::insert(const HistoryRecord& r) {
    if (!db_) return;
    const char* sql =
        "INSERT INTO sessions(game_name,start_time,end_time,"
        "avg_ping_ms,avg_jitter_ms,avg_packet_loss,max_ping_ms,"
        "quality_score,boost_was_active,duration_seconds)"
        "VALUES(?,?,?,?,?,?,?,?,?,?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;

    sqlite3_bind_text(stmt, 1, r.game_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, r.start_time_str.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, r.end_time_str.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 4, r.avg_ping_ms);
    sqlite3_bind_double(stmt, 5, r.avg_jitter_ms);
    sqlite3_bind_double(stmt, 6, r.avg_packet_loss);
    sqlite3_bind_double(stmt, 7, r.max_ping_ms);
    sqlite3_bind_double(stmt, 8, r.quality_score);
    sqlite3_bind_int(stmt, 9, r.boost_was_active ? 1 : 0);
    sqlite3_bind_int(stmt, 10, r.duration_seconds);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::vector<HistoryRecord> SqliteHistoryStore::getLast(int count) const {
    std::vector<HistoryRecord> out;
    if (!db_) return out;
    const char* sql =
        "SELECT game_name,start_time,end_time,avg_ping_ms,avg_jitter_ms,"
        "avg_packet_loss,max_ping_ms,quality_score,boost_was_active,duration_seconds "
        "FROM sessions ORDER BY id DESC LIMIT ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return out;
    sqlite3_bind_int(stmt, 1, count);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        HistoryRecord r;
        r.game_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        r.start_time_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        r.end_time_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        r.avg_ping_ms = sqlite3_column_double(stmt, 3);
        r.avg_jitter_ms = sqlite3_column_double(stmt, 4);
        r.avg_packet_loss = sqlite3_column_double(stmt, 5);
        r.max_ping_ms = sqlite3_column_double(stmt, 6);
        r.quality_score = sqlite3_column_double(stmt, 7);
        r.boost_was_active = sqlite3_column_int(stmt, 8) != 0;
        r.duration_seconds = sqlite3_column_int(stmt, 9);
        out.push_back(std::move(r));
    }
    sqlite3_finalize(stmt);
    return out;
}

double SqliteHistoryStore::averageScore() const {
    if (!db_) return 0;
    const char* sql = "SELECT AVG(quality_score) FROM sessions WHERE quality_score > 0;";
    sqlite3_stmt* stmt = nullptr;
    double result = 0;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW)
            result = sqlite3_column_double(stmt, 0);
        sqlite3_finalize(stmt);
    }
    return result;
}

int SqliteHistoryStore::totalSessions() const {
    if (!db_) return 0;
    const char* sql = "SELECT COUNT(*) FROM sessions;";
    sqlite3_stmt* stmt = nullptr;
    int count = 0;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW)
            count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    return count;
}

} // namespace gno
