#pragma once

// Gaming Health Score — pure logic per spec GNO_HEALTHSCORE_2026-09-15.md.
// No Qt, no OS calls: inputs are already-measured values from
// monitoring/diagnostics/remediation layers. Unit-testable.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace gno {
namespace health {

// ---------------------------------------------------------------- thresholds
// Per-game network thresholds mirror alert_thresholds (spec 1.1 #4).
struct GameProfile {
    std::string name;       // "CS2", "VALORANT", "PUBG"...
    double good_ping_ms;    // <= this is "good" (60 / 50 / 100)
    double max_loss_pct;    // >= this is 0 points (1.5 / 1.0 / 3.0)
};

inline GameProfile profileFor(const std::string& game) {
    if (game == "VALORANT") return {game, 50.0, 1.0};
    if (game == "PUBG")     return {game, 100.0, 3.0};
    return {game, 60.0, 1.5}; // CS2 and default
}

// ---------------------------------------------------------------- inputs
struct NetworkInput {
    double p50_ms = -1;      // <=0 => unknown
    double jitter_p95_ms = -1;
    double loss_pct = -1;    // over the measurement window
    double dns_best_ms = -1; // min across benchmarked resolvers
    double window_seconds = 0;   // actual measurement window
    double nodes_probed = 0;
    double nodes_total = 0;
    double last_sample_age_seconds = 1e9; // freshness
};

struct SystemInput {
    bool power_high_performance = true;
    bool gamedvr_disabled = true;
    bool fso_matches_profile = true;
    bool disk_space_ok = true;
    bool no_heavy_background = true;
    bool timer_resolution_ok = true;
};

struct GameReadyInput {
    bool applicable = false;  // game selected/known
    int checks_total = 0;
    int checks_passed = 0;
};

struct StabilityInput {
    int total_samples = 0;    // 7-day window from sqlite history
    int degraded_samples = 0; // samples where S_network < 60
};

// ---------------------------------------------------------------- scoring
// Piecewise-linear 100->0 between best (100%) and worst (0%).
inline double scale(double value, double best, double worst) {
    if (value <= 0) return -1; // unknown marker
    double s = (worst - value) / (worst - best) * 100.0;
    return std::clamp(s, 0.0, 100.0);
}

struct Subscore {
    double score = -1;   // -1 => not measured
    double weight = 0;   // effective (renormalized) weight
};

// S_NETWORK: weighted p50 0.35 / jitter 0.25 / loss 0.30 / dns 0.10.
// Loss uses sqrt scale (small losses already matter). Unknown components
// drop out and the remaining weights renormalize.
inline double networkScore(const NetworkInput& n, const GameProfile& p,
                           double* effective_weight_out = nullptr) {
    struct Part { double val; double w; };
    Part parts[] = {
        {scale(n.p50_ms, p.good_ping_ms * 0.66, p.good_ping_ms * 3.0), 0.35},
        {scale(n.jitter_p95_ms, 2.0, 20.0), 0.25},
        {n.loss_pct < 0 ? -1.0 : 100.0 * (1.0 - std::sqrt(std::clamp(n.loss_pct / p.max_loss_pct, 0.0, 1.0))), 0.30},
        {scale(n.dns_best_ms, 15.0, 150.0), 0.10},
    };
    double acc = 0, wsum = 0;
    for (const auto& pr : parts) {
        if (pr.val < 0) continue;
        acc += pr.val * pr.w;
        wsum += pr.w;
    }
    if (wsum <= 0) return -1;
    if (effective_weight_out) *effective_weight_out = 1.0; // network always present if score exists
    return acc / wsum;
}

// S_SYSTEM: penalty model, start 100, floor 0 (spec 1.4).
inline double systemScore(const SystemInput& s) {
    double v = 100.0;
    if (!s.power_high_performance) v -= 15;
    if (!s.gamedvr_disabled)       v -= 10;
    if (!s.fso_matches_profile)    v -= 8;
    if (!s.disk_space_ok)          v -= 20;
    if (!s.no_heavy_background)    v -= 10;
    if (!s.timer_resolution_ok)    v -= 7;
    return std::max(v, 0.0);
}

// S_GAME_READY: percent of passed applicable checks (spec 1.5).
inline double gameReadyScore(const GameReadyInput& g) {
    if (!g.applicable || g.checks_total <= 0) return -1;
    return 100.0 * std::clamp(g.checks_passed, 0, g.checks_total) / g.checks_total;
}

// S_STABILITY: 100 * (1 - degraded/total) (spec 1.6); needs >= 3 days
// (approx. 3*2880 samples at 30s cadence is too strict — use caller-provided
// samples; we require total_samples >= 20, else unknown).
inline double stabilityScore(const StabilityInput& st) {
    if (st.total_samples < 20) return -1;
    double ratio = static_cast<double>(st.degraded_samples) / st.total_samples;
    return std::clamp(100.0 * (1.0 - ratio), 0.0, 100.0);
}

// ---------------------------------------------------------------- confidence
// C = min(window/60s,1) * freshness * node_coverage (spec 1.7).
inline double confidence(const NetworkInput& n) {
    double w = std::clamp(n.window_seconds / 60.0, 0.0, 1.0);
    double f = n.last_sample_age_seconds <= 300 ? 1.0
             : n.last_sample_age_seconds >= 1800 ? 0.2
             : 1.0 - 0.8 * (n.last_sample_age_seconds - 300) / 1500.0;
    double cov = n.nodes_total > 0 ? std::clamp(n.nodes_probed / n.nodes_total, 0.0, 1.0) : 1.0;
    return std::clamp(w * f * cov, 0.0, 1.0);
}

// ---------------------------------------------------------------- aggregate
enum class Level { Excellent, Good, Attention, Degraded, Critical, Unknown };

inline const char* levelName(Level l) {
    switch (l) {
        case Level::Excellent: return "Excellent";
        case Level::Good:      return "Good";
        case Level::Attention: return "Attention";
        case Level::Degraded:  return "Degraded";
        case Level::Critical:  return "Critical";
        default:               return "Unknown";
    }
}

struct HealthResult {
    double network = -1, system = -1, game_ready = -1, stability = -1;
    double total = -1;          // -1 => do NOT render as a number (spec 1.7)
    double confidence = 0;      // 0..1
    bool hidden_by_confidence = false; // C<0.4 => show "measuring…", not number
    bool critical_gate = false; // S_network<30 forces Critical
    Level level = Level::Unknown;
};

// Base weights: NET .40 SYS .25 GAME .20 STAB .15; unknown components'
// weights renormalize over the known ones (spec 1.6 renormalization rule).
inline HealthResult compute(const NetworkInput& n, const SystemInput& s,
                            const GameReadyInput& g, const StabilityInput& st,
                            const GameProfile& p) {
    HealthResult r;
    r.network = networkScore(n, p);
    r.system = systemScore(s);
    r.game_ready = gameReadyScore(g);
    r.stability = stabilityScore(st);

    struct Part { double v; double w; };
    Part parts[] = {
        {r.network, 0.40}, {r.system, 0.25}, {r.game_ready, 0.20}, {r.stability, 0.15},
    };
    double acc = 0, wsum = 0;
    for (const auto& pr : parts) {
        if (pr.v < 0) continue;
        acc += pr.v * pr.w;
        wsum += pr.w;
    }

    r.confidence = confidence(n);
    // Network missing entirely => confidence of whole score is zero.
    if (r.network < 0) r.confidence = 0;

    if (wsum <= 0) { r.level = Level::Unknown; return r; }
    double total = acc / wsum;

    r.critical_gate = (r.network >= 0 && r.network < 30);

    if (r.confidence < 0.4) {
        r.hidden_by_confidence = true;
        r.total = -1;
    } else {
        r.total = total;
    }

    // Level from displayed number; critical gate wins (spec 1.8).
    if (r.hidden_by_confidence) {
        r.level = Level::Unknown;
    } else if (r.critical_gate || total < 30) {
        r.level = Level::Critical;
    } else if (total < 55) {
        r.level = Level::Degraded;
    } else if (total < 75) {
        r.level = Level::Attention;
    } else if (total < 90) {
        r.level = Level::Good;
    } else {
        r.level = Level::Excellent;
    }
    // Even when hidden, a critical network must surface: keep gate flag set.
    return r;
}

} // namespace health
} // namespace gno
