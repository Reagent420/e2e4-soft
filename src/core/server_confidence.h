#pragma once

// Server-node confidence model (spec 1.7 / screens-spec §7): "best server"
// must never rest on a single ping. Pure logic, no Qt — feeds geo_map UI.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace gno {
namespace servermap {

struct ProbeSample {
    std::int64_t unix_ms = 0;   // when measured
    double latency_ms = -1;     // -1 failed / timeout
};

// Minimum samples to grade a node at all (product rule: >=3 measurements).
inline constexpr int kMinSamples = 3;
// Samples older than this don't count toward "current" state.
inline constexpr std::int64_t kFreshWindowMs = 6LL * 3600 * 1000; // 6 h

enum class Confidence { None, Low, Medium, High };

inline const char* confidenceName(Confidence c) {
    switch (c) {
        case Confidence::None: return "no-data";
        case Confidence::Low: return "low";
        case Confidence::Medium: return "medium";
        case Confidence::High: return "high";
    }
    return "no-data";
}

struct NodeEstimate {
    bool valid = false;
    double p50_ms = -1;      // representative latency (median of fresh samples)
    double p95_ms = -1;
    double jitter_spread = -1; // p95 - p50: stability proxy
    double availability = 0;   // success ratio of fresh samples
    int fresh_count = 0;
    Confidence confidence = Confidence::None;
    std::string rationale;     // shown in UI tooltip
};

// Core estimator: filters window, computes robust stats + confidence.
inline NodeEstimate estimate(const std::vector<ProbeSample>& samples,
                             std::int64_t now_ms) {
    NodeEstimate e;
    std::vector<ProbeSample> fresh;
    fresh.reserve(samples.size());
    for (const auto& s : samples) {
        if (s.unix_ms <= now_ms && now_ms - s.unix_ms <= kFreshWindowMs)
            fresh.push_back(s);
    }
    e.fresh_count = static_cast<int>(fresh.size());
    if (fresh.empty()) {
        e.rationale = "нет замеров за 6 часов";
        return e;
    }

    std::vector<double> ok;
    int failures = 0;
    for (auto& s : fresh) {
        if (s.latency_ms > 0) ok.push_back(s.latency_ms);
        else ++failures;
    }
    e.availability = static_cast<double>(ok.size()) / fresh.size();

    if (ok.size() < static_cast<std::size_t>(kMinSamples)) {
        // Still show the value, but never with high confidence.
        if (!ok.empty()) {
            std::sort(ok.begin(), ok.end());
            e.p50_ms = ok[ok.size() / 2];
            e.valid = true;
        }
        e.confidence = e.valid ? Confidence::Low : Confidence::None;
        e.rationale = "мало данных (" + std::to_string(ok.size()) + " успешных из " +
                      std::to_string(fresh.size()) + ")";
        return e;
    }

    std::sort(ok.begin(), ok.end());
    e.p50_ms = ok[ok.size() / 2];
    e.p95_ms = ok[std::min(ok.size() - 1, static_cast<std::size_t>(ok.size() * 0.95))];
    e.jitter_spread = e.p95_ms - e.p50_ms;
    e.valid = true;

    // Confidence: sample count + availability + spread stability.
    int score = 0;
    score += ok.size() >= 12 ? 2 : 1;                       // >=12 probes in window
    score += e.availability >= 0.95 ? 2 : (e.availability >= 0.8 ? 1 : 0);
    double rel_spread = e.p50_ms > 0 ? e.jitter_spread / e.p50_ms : 1.0;
    score += rel_spread <= 0.25 ? 2 : (rel_spread <= 0.6 ? 1 : 0);
    if (score >= 5) e.confidence = Confidence::High;
    else if (score >= 3) e.confidence = Confidence::Medium;
    else e.confidence = Confidence::Low;
    e.rationale = std::to_string(ok.size()) + " замеров, доступность " +
                  std::to_string(static_cast<int>(e.availability * 100)) + "%, разброс " +
                  std::to_string(static_cast<int>(e.jitter_spread)) + " мс";
    return e;
}

// Compare two nodes strictly: better only when both have Medium+ confidence
// and p50 differs by more than the noise floor (max of both spreads).
inline bool confidentBetter(const NodeEstimate& a, const NodeEstimate& b) {
    const auto tier = [](Confidence c) { return static_cast<int>(c); };
    if (tier(a.confidence) < static_cast<int>(Confidence::Medium) ||
        tier(b.confidence) < static_cast<int>(Confidence::Medium))
        return false;
    if (!a.valid || !b.valid) return false;
    double noise = std::max(a.jitter_spread, b.jitter_spread);
    return (b.p50_ms - a.p50_ms) > noise; // a meaningfully better than b
}

} // namespace servermap
} // namespace gno
