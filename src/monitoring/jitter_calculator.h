#pragma once

#include <vector>
#include <cstdint>
#include <chrono>
#include <mutex>
#include <atomic>

namespace gno {

struct JitterSample {
    std::chrono::steady_clock::time_point timestamp;
    double latency_ms = 0.0;
    double jitter_ms = 0.0;
};

struct JitterStats {
    double current_jitter_ms = 0.0;
    double average_jitter_ms = 0.0;
    double max_jitter_ms = 0.0;
    double min_jitter_ms = 999999.0;
    std::vector<JitterSample> samples;
    uint32_t sample_count = 0;
};

class JitterCalculator {
public:
    JitterCalculator();
    ~JitterCalculator();

    void addSample(double latency_ms);
    void reset();
    
    JitterStats getStats() const;
    double calculateJitter() const;
    
    void setWindowSize(uint32_t window_size);
    uint32_t getWindowSize() const;

private:
    mutable std::mutex samples_mutex_;
    std::vector<double> latency_samples_;
    std::vector<JitterSample> jitter_samples_;
    uint32_t window_size_ = 50;
    double last_latency_ = 0.0;
    bool has_last_ = false;
};

} // namespace gno
