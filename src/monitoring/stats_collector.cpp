#include "stats_collector.h"
#include <fstream>
#include <sstream>
#include <iomanip>

namespace gno {

StatsCollector::StatsCollector() = default;
StatsCollector::~StatsCollector() = default;

void StatsCollector::start(const std::string& session_name) {
    std::lock_guard<std::mutex> lock(session_mutex_);
    
    current_session_name_ = session_name;
    current_session_ = SessionStats{};
    current_session_.start_time = std::chrono::steady_clock::now();
    recording_ = true;
}

void StatsCollector::stop() {
    std::lock_guard<std::mutex> lock(session_mutex_);
    
    current_session_.end_time = std::chrono::steady_clock::now();
    past_sessions_.push_back(current_session_);
    recording_ = false;
    
    if (session_callback_) {
        session_callback_(current_session_);
    }
}

bool StatsCollector::isRecording() const {
    return recording_;
}

void StatsCollector::recordSnapshot(const NetworkSnapshot& snapshot) {
    std::lock_guard<std::mutex> lock(session_mutex_);
    
    if (!recording_) return;
    
    current_session_.snapshots.push_back(snapshot);
    
    if (current_session_.snapshots.size() > max_snapshots_) {
        current_session_.snapshots.erase(current_session_.snapshots.begin());
    }
    
    current_session_.total_ping_ms += snapshot.ping_ms;
    current_session_.total_jitter_ms += snapshot.jitter_ms;
    current_session_.total_packet_loss_percent += snapshot.packet_loss_percent;
    current_session_.total_samples++;
}

void StatsCollector::recordPing(double latency_ms) {
    std::lock_guard<std::mutex> lock(session_mutex_);
    
    if (!recording_) return;
    
    current_session_.total_ping_ms += latency_ms;
    current_session_.total_samples++;
}

void StatsCollector::recordPacketLoss(double loss_percent) {
    std::lock_guard<std::mutex> lock(session_mutex_);
    
    if (!recording_) return;
    
    current_session_.total_packet_loss_percent += loss_percent;
}

void StatsCollector::recordJitter(double jitter_ms) {
    std::lock_guard<std::mutex> lock(session_mutex_);
    
    if (!recording_) return;
    
    current_session_.total_jitter_ms += jitter_ms;
}

SessionStats StatsCollector::getCurrentSession() const {
    std::lock_guard<std::mutex> lock(session_mutex_);
    return current_session_;
}

std::vector<SessionStats> StatsCollector::getPastSessions() const {
    std::lock_guard<std::mutex> lock(session_mutex_);
    return past_sessions_;
}

bool StatsCollector::saveSession(const std::string& filepath) const {
    std::lock_guard<std::mutex> lock(session_mutex_);
    
    std::ofstream file(filepath);
    if (!file.is_open()) return false;
    
    file << "Session: " << current_session_name_ << "\n";
    file << "Samples: " << current_session_.total_samples << "\n";
    
    if (current_session_.total_samples > 0) {
        file << "Average Ping: " << (current_session_.total_ping_ms / current_session_.total_samples) << " ms\n";
        file << "Average Jitter: " << (current_session_.total_jitter_ms / current_session_.total_samples) << " ms\n";
        file << "Average Packet Loss: " << (current_session_.total_packet_loss_percent / current_session_.total_samples) << "%\n";
    }
    
    file << "\nSnapshots:\n";
    for (const auto& snap : current_session_.snapshots) {
        auto time = std::chrono::duration_cast<std::chrono::seconds>(
            snap.timestamp.time_since_epoch()).count();
        file << time << "," << snap.ping_ms << "," << snap.jitter_ms << ","
             << snap.packet_loss_percent << "\n";
    }
    
    return true;
}

bool StatsCollector::loadSession(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return false;
    
    std::string line;
    std::getline(file, line);
    
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string token;
        
        NetworkSnapshot snap;
        std::getline(ss, token, ',');
        snap.ping_ms = std::stod(token);
        std::getline(ss, token, ',');
        snap.jitter_ms = std::stod(token);
        std::getline(ss, token, ',');
        snap.packet_loss_percent = std::stod(token);
        
        current_session_.snapshots.push_back(snap);
    }
    
    return true;
}

void StatsCollector::setMaxSnapshots(uint32_t max) {
    max_snapshots_ = max;
}

uint32_t StatsCollector::getSnapshotCount() const {
    std::lock_guard<std::mutex> lock(session_mutex_);
    return current_session_.snapshots.size();
}

void StatsCollector::setSessionCallback(SessionCallback callback) {
    session_callback_ = std::move(callback);
}

} // namespace gno
