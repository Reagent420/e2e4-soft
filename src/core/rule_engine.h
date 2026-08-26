#pragma once

// Rules engine (v3.1): "when metric exceeds threshold → trigger action".
// Rules are stored in QSettings as JSON-like entries.
// Pure logic, fully testable.

#include "core/alert_thresholds.h"

#include <string>
#include <vector>

namespace gno {

struct AutomationRule {
    std::string id;
    std::string name;
    // Condition
    enum class Metric { Ping = 0, Jitter = 1, Loss = 2 };
    Metric metric = Metric::Ping;
    double threshold = 100.0;
    // Action (legacy bridge id)
    std::string action_id; // "dns", "game_dvr", "power_plan", etc.
    bool enabled = true;

    bool triggers(double ping, double jitter, double loss) const {
        switch (metric) {
            case Metric::Ping:   return ping > threshold;
            case Metric::Jitter: return jitter > threshold;
            case Metric::Loss:   return loss > threshold;
        }
        return false;
    }
};

class RuleEngine {
public:
    static std::vector<AutomationRule> defaultRules() {
        return {
            {"rule_high_ping", "High ping → power plan", 
             AutomationRule::Metric::Ping, 120.0, "power_plan", true},
            {"rule_high_loss", "Packet loss → Game DVR off",
             AutomationRule::Metric::Loss, 3.0, "game_dvr", true},
            {"rule_jitter", "Jitter spike → TCP tuning",
             AutomationRule::Metric::Jitter, 15.0, "tcp", true},
        };
    }

    // Returns rules that trigger with current metrics AND are enabled.
    static std::vector<const AutomationRule*> evaluate(
        const std::vector<AutomationRule>& rules,
        double ping, double jitter, double loss)
    {
        std::vector<const AutomationRule*> hit;
        for (const auto& r : rules) {
            if (!r.enabled) continue;
            if (r.triggers(ping, jitter, loss))
                hit.push_back(&r);
        }
        return hit;
    }
};

} // namespace gno
