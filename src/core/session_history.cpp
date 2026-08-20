#include "session_history.h"

#include "input_validation.h"
#include "json_persistence.h"

#include <ctime>
#include <nlohmann/json.hpp>
#include <optional>

namespace {

constexpr std::size_t kMaxHistoryBytes = 4 * 1024 * 1024;
constexpr std::size_t kMaxHistoryRecords = 500;
constexpr int kHistoryDocumentVersion = 1;

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

std::optional<std::vector<gno::SessionRecord>> parseHistoryDocument(const std::string& content) {
    try {
        const auto root = nlohmann::json::parse(content);
        const nlohmann::json* items = nullptr;
        if (root.is_array()) {
            items = &root; // Legacy documents are version 1.
        } else if (root.is_object() && root.contains("records") && root.at("records").is_array() &&
                   root.value("version", kHistoryDocumentVersion) == kHistoryDocumentVersion) {
            items = &root.at("records");
        }
        if (!items || items->size() > kMaxHistoryRecords) return std::nullopt;

        std::vector<gno::SessionRecord> records;
        records.reserve(items->size());
        for (const auto& item : *items) records.push_back(recordFromJson(item));
        return records;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

nlohmann::json historyDocument(const std::vector<gno::SessionRecord>& records) {
    nlohmann::json items = nlohmann::json::array();
    for (const auto& record : records) items.push_back(recordToJson(record));
    return {{"version", kHistoryDocumentVersion}, {"records", std::move(items)}};
}

std::string currentTimeStr() {
    const auto now = std::time(nullptr);
    char buffer[64];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    return buffer;
}

} // namespace

namespace gno {

SessionHistory::SessionHistory(std::filesystem::path storage_root)
    : storage_root_(std::move(storage_root)) {
    loadFromFile();
}

std::string SessionHistory::getSavePath() const {
    return persistence::storageFile(storage_root_, "history.json").string();
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

    auto completed = current_;
    completed.end_time_str = currentTimeStr();
    completed.avg_ping_ms = avg_ping;
    completed.avg_jitter_ms = avg_jitter;
    completed.avg_packet_loss = loss;
    completed.max_ping_ms = max_ping;
    completed.duration_seconds = 0;

    auto candidate = records_;
    candidate.push_back(std::move(completed));
    if (candidate.size() > kMaxHistoryRecords) candidate.erase(candidate.begin());
    if (!saveRecords(candidate, getSavePath())) return;

    records_ = std::move(candidate);
    recording_ = false;
}

std::vector<SessionRecord> SessionHistory::getAll() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return records_;
}

std::vector<SessionRecord> SessionHistory::getLast(int count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (count <= 0) return {};
    if (static_cast<std::size_t>(count) >= records_.size()) return records_;
    return std::vector<SessionRecord>(records_.end() - count, records_.end());
}

void SessionHistory::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::vector<SessionRecord> candidate;
    if (saveRecords(candidate, getSavePath())) records_ = candidate;
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

bool SessionHistory::saveRecords(const std::vector<SessionRecord>& records, const std::string& path) const {
    if (records.size() > kMaxHistoryRecords) return false;
    return persistence::atomicWriteText(path, historyDocument(records).dump(2));
}

bool SessionHistory::saveToFileUnlocked(const std::string& path) const {
    return saveRecords(records_, path);
}

bool SessionHistory::loadFromFile(const std::string& path) {
    const auto content = readBoundedFile(path.empty() ? getSavePath() : path, kMaxHistoryBytes);
    if (!content) return false;
    auto parsed = parseHistoryDocument(*content);
    if (!parsed) return false;

    std::lock_guard<std::mutex> lock(mutex_);
    records_ = std::move(*parsed);
    return true;
}

} // namespace gno
