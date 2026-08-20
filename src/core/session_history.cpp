#include "session_history.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <ctime>

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#include <shlobj.h>
#endif

namespace gno {

static std::string currentTimeStr() {
    auto now = std::time(nullptr);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    return buf;
}

static std::time_t parseTimeStr(const std::string& str) {
    std::tm tm = {};
    std::istringstream ss(str);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (ss.fail()) return 0;
    return std::mktime(&tm);
}

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

    auto start = parseTimeStr(current_.start_time_str);
    auto end = parseTimeStr(current_.end_time_str);
    current_.duration_seconds = (end > start) ? static_cast<int>(end - start) : 0;

    records_.push_back(current_);
    recording_ = false;

    if (records_.size() > 500) {
        records_.erase(records_.begin());
    }

    saveToFile();
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
    saveToFile();
}

double SessionHistory::getAveragePing() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (records_.empty()) return 0.0;
    double sum = 0;
    for (const auto& r : records_) sum += r.avg_ping_ms;
    return sum / records_.size();
}

double SessionHistory::getAverageJitter() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (records_.empty()) return 0.0;
    double sum = 0;
    for (const auto& r : records_) sum += r.avg_jitter_ms;
    return sum / records_.size();
}

bool SessionHistory::saveToFile(const std::string& path) const {
    std::string p = path.empty() ? getSavePath() : path;
    std::string dir = p.substr(0, p.find_last_of("\\/"));
#ifdef PLATFORM_WINDOWS
    CreateDirectoryA(dir.c_str(), nullptr);
#endif

    std::lock_guard<std::mutex> lock(mutex_);
    std::ofstream file(p);
    if (!file.is_open()) return false;

    file << "[\n";
    for (size_t i = 0; i < records_.size(); ++i) {
        const auto& r = records_[i];
        file << "  {\n";
        file << "    \"game\": \"" << r.game_name << "\",\n";
        file << "    \"start\": \"" << r.start_time_str << "\",\n";
        file << "    \"end\": \"" << r.end_time_str << "\",\n";
        file << "    \"avg_ping\": " << r.avg_ping_ms << ",\n";
        file << "    \"avg_jitter\": " << r.avg_jitter_ms << ",\n";
        file << "    \"avg_loss\": " << r.avg_packet_loss << ",\n";
        file << "    \"max_ping\": " << r.max_ping_ms << ",\n";
        file << "    \"duration\": " << r.duration_seconds << ",\n";
        file << "    \"boost\": " << (r.boost_was_active ? "true" : "false") << "\n";
        file << "  }";
        if (i < records_.size() - 1) file << ",";
        file << "\n";
    }
    file << "]\n";
    return true;
}

bool SessionHistory::loadFromFile(const std::string& path) {
    std::string p = path.empty() ? getSavePath() : path;
    std::ifstream file(p);
    if (!file.is_open()) return false;

    std::lock_guard<std::mutex> lock(mutex_);
    records_.clear();

    std::string line;
    SessionRecord current;
    bool inRecord = false;

    while (std::getline(file, line)) {
        auto trim = [](std::string& s) {
            s.erase(0, s.find_first_not_of(" \t\r\n{"));
            s.erase(s.find_last_not_of(" \t\r\n},") + 1);
        };

        if (line.find("\"game\"") != std::string::npos) {
            if (inRecord && !current.game_name.empty()) records_.push_back(current);
            current = SessionRecord{};
            inRecord = true;
            auto pos = line.find(':');
            if (pos != std::string::npos) {
                current.game_name = line.substr(pos + 1);
                trim(current.game_name);
                current.game_name.erase(
                    std::remove(current.game_name.begin(), current.game_name.end(), '"'),
                    current.game_name.end());
            }
        } else if (line.find("\"start\"") != std::string::npos) {
            auto pos = line.find(':');
            if (pos != std::string::npos) {
                current.start_time_str = line.substr(pos + 1);
                trim(current.start_time_str);
                current.start_time_str.erase(
                    std::remove(current.start_time_str.begin(), current.start_time_str.end(), '"'),
                    current.start_time_str.end());
            }
        } else if (line.find("\"avg_ping\"") != std::string::npos) {
            auto pos = line.find(':');
            if (pos != std::string::npos) current.avg_ping_ms = std::stod(line.substr(pos + 1));
        } else if (line.find("\"avg_jitter\"") != std::string::npos) {
            auto pos = line.find(':');
            if (pos != std::string::npos) current.avg_jitter_ms = std::stod(line.substr(pos + 1));
        } else if (line.find("\"avg_loss\"") != std::string::npos) {
            auto pos = line.find(':');
            if (pos != std::string::npos) current.avg_packet_loss = std::stod(line.substr(pos + 1));
        } else if (line.find("\"boost\"") != std::string::npos) {
            current.boost_was_active = line.find("true") != std::string::npos;
        }
    }

    if (inRecord && !current.game_name.empty()) records_.push_back(current);
    return true;
}

} // namespace gno
