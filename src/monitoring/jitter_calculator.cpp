#include "jitter_calculator.h"
#include <algorithm>
#include <cmath>

namespace gno {

JitterCalculator::JitterCalculator() = default;
JitterCalculator::~JitterCalculator() = default;

void JitterCalculator::addSample(double latency_ms) {
    std::lock_guard<std::mutex> lock(samples_mutex_);
    
    latency_samples_.push_back(latency_ms);
    
    if (latency_samples_.size() > window_size_) {
        latency_samples_.erase(latency_samples_.begin());
    }
    
    if (has_last_) {
        double jitter = std::abs(latency_ms - last_latency_);
        
        JitterSample sample;
        sample.timestamp = std::chrono::steady_clock::now();
        sample.latency_ms = latency_ms;
        sample.jitter_ms = jitter;
        
        jitter_samples_.push_back(sample);
        
        if (jitter_samples_.size() > window_size_) {
            jitter_samples_.erase(jitter_samples_.begin());
        }
    }
    
    last_latency_ = latency_ms;
    has_last_ = true;
}

void JitterCalculator::reset() {
    std::lock_guard<std::mutex> lock(samples_mutex_);
    latency_samples_.clear();
    jitter_samples_.clear();
    last_latency_ = 0.0;
    has_last_ = false;
}

JitterStats JitterCalculator::getStats() const {
    std::lock_guard<std::mutex> lock(samples_mutex_);
    
    JitterStats stats;
    stats.sample_count = jitter_samples_.size();
    stats.samples = jitter_samples_;
    
    if (!jitter_samples_.empty()) {
        stats.current_jitter_ms = jitter_samples_.back().jitter_ms;
        
        double total_jitter = 0.0;
        stats.max_jitter_ms = 0.0;
        stats.min_jitter_ms = 999999.0;
        
        for (const auto& sample : jitter_samples_) {
            total_jitter += sample.jitter_ms;
            stats.max_jitter_ms = std::max(stats.max_jitter_ms, sample.jitter_ms);
            stats.min_jitter_ms = std::min(stats.min_jitter_ms, sample.jitter_ms);
        }
        
        stats.average_jitter_ms = total_jitter / jitter_samples_.size();
    }
    
    return stats;
}

double JitterCalculator::calculateJitter() const {
    std::lock_guard<std::mutex> lock(samples_mutex_);
    
    if (latency_samples_.size() < 2) return 0.0;
    
    double sum = 0.0;
    double prev_latency = latency_samples_[0];
    
    for (size_t i = 1; i < latency_samples_.size(); i++) {
        sum += std::abs(latency_samples_[i] - prev_latency);
        prev_latency = latency_samples_[i];
    }
    
    return sum / (latency_samples_.size() - 1);
}

void JitterCalculator::setWindowSize(uint32_t window_size) {
    window_size_ = window_size;
}

uint32_t JitterCalculator::getWindowSize() const {
    return window_size_;
}

} // namespace gno
