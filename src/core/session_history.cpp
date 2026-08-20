#include "session_history.h"

#include "input_validation.h"

#include <ctime>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#ifdef PLATFORM_WINDOWS
#include <shlobj.h>
#include <windows.h>
#endif

namespace {

constexpr std::size_t kMaxHistoryBytes = 4 * 1024 * 1024;
constexpr std::size_t kMaxHistoryRecords = 500;

bool ensureParentDirectory(const std::string& path) {
    const auto parent = std::filesystem::path(path).parent_path();
    if (parent.empty()) return true;

    std::error_code error;
    std::filesystem::create_directories(parent, error);
    return !error;
}

nlohmann::json recordToJson(const gno::SessionRecord& record) {
    return {{"game", record.game_name},
            {"start", record.start_time_str},
            {"end", record.end_time_str},
            {"avg_ping", record.avg_ping_ms},
            {"avg_jitter", record.avg_jitter_ms},
            {"avg_loss", record.avg_packet_loss},
            {"max_ping", record.max_ping_ms},
            {"duration", record.duration_seconds},
            {"boost", record.boost_was_active}};
}

gno::SessionRecord recordFromJson(const nlohmann::json& value) {
    gno::SessionRecord record;
    record.game_name = value.value("game", std::string{});
    record.start_time_str = value.value("start", std::string{});
    record.end_time_str = value.value("end", std::string{});
    record.avg_ping_ms = value.value("avg_ping", 0.0);
    record.avg_jitter_ms = value.value("avg_jitter", 0.0);
    record.avg_packet_loss = value.value("avg_loss", 0.0);
    record.max_ping_ms = value.value("max_ping", 0.0);
    record.duration_seconds = value.value("duration", uint32_t{0});
    record.boost_was_active = value.value("boost", false);
    return record;
}

std::string currentTimeStr() {
    const auto now = std::time(nullptr);
    char buffer[64];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    return buffer;
}

} // namespace

namespace gno {

SessionHistory::SessionHistory() {
    loadFromFile();
}

SessionHistory::~SessionHistory() {
    saveToFile();
}

std::string SessionHistory::getAppDataPath() const {
#ifdef PLATFORM_WINDOWS
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, path))) {
        return std::string(path);
    }
#endif
    return ".";
}

std::string SessionHistory::getSavePath() const {
    return getAppDataPath() + "\\GNO\\history.json";
}

void SessionHistory::recordStart(const std::string& game_name, bool boost) {
    std::lock_guard<std::mutex> lock(mutex_);
    current_ = SessionRecord{};
    current_.game_name = game_name;
    current_.start_time_str = currentTimeStr();
    current_.boost_was_active = boost;
    recording_ = true;
}

void SessionHistory::recordEnd(double avg_ping, double avg_jitter, double loss, double max_ping) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!recording_) return;

    current_.end_time_str = currentTimeStr();
    current_.avg_ping_ms = avg_ping;
    current_.avg_jitter_ms = avg_jitter;
    current_.avg_packet_loss = loss;
    current_.max_ping_ms = max_ping;
    current_.duration_seconds = 0;
    records_.push_back(current_);
    recording_ = false;

    if (records_.size() > kMaxHistoryRecords) records_.erase(records_.begin());
    saveToFileUnlocked(getSavePath());
}

std::vector<SessionRecord> SessionHistory::getAll() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return records_;
}

std::vector<SessionRecord> SessionHistory::getLast(int count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (static_cast<int>(records_.size()) <= count) return records_;
    return std::vector<SessionRecord>(records_.end() - count, records_.end());
}

void SessionHistory::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    records_.clear();
    saveToFileUnlocked(getSavePath());
}

double SessionHistory::getAveragePing() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (records_.empty()) return 0.0;
    double sum = 0;
    for (const auto& record : records_) sum += record.avg_ping_ms;
    return sum / records_.size();
}

double SessionHistory::getAverageJitter() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (records_.empty()) return 0.0;
    double sum = 0;
    for (const auto& record : records_) sum += record.avg_jitter_ms;
    return sum / records_.size();
}

bool SessionHistory::saveToFile(const std::string& path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return saveToFileUnlocked(path.empty() ? getSavePath() : path);
}

bool SessionHistory::saveToFileUnlocked(const std::string& path) const {
    if (!ensureParentDirectory(path)) return false;

    nlohmann::json records = nlohmann::json::array();
    for (const auto& record : records_) records.push_back(recordToJson(record));

    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << records.dump(2);
    file.flush();
    file.close();
    return static_cast<bool>(file);
}

bool SessionHistory::loadFromFile(const std::string& path) {
    const auto content = readBoundedFile(path.empty() ? getSavePath() : path, kMaxHistoryBytes);
    if (!content) return false;

    try {
        const auto root = nlohmann::json::parse(*content);
        if (!root.is_array() || root.size() > kMaxHistoryRecords) return false;

        std::vector<SessionRecord> parsed;
        parsed.reserve(root.size());
        for (const auto& item : root) parsed.push_back(recordFromJson(item));

        std::lock_guard<std::mutex> lock(mutex_);
        records_ = std::move(parsed);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

} // namespace gno
