#pragma once

// Builds a portable JSON diagnostic report (v1.8.0). Pure logic, testable.

#include <sstream>
#include <string>
#include <vector>

namespace gno {

class ReportExporter {
public:
    struct Check {
        std::string name;
        int severity = 0; // 0 ok, 1 warn, 2 critical
    };

    static std::string escape(const std::string& s) {
        std::string out;
        for (char c : s) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                default: if (static_cast<unsigned char>(c) < 0x20) { /* skip */ } else out += c;
            }
        }
        return out;
    }

    static std::string build(const std::string& game,
                             double avg_ping, double jitter, double loss_percent,
                             int quality_score,
                             const std::string& best_server, int best_latency,
                             const std::vector<Check>& checks) {
        std::ostringstream o;
        o << "{\n  \"app\": \"GNO\",\n";
        o << "  \"game\": \"" << escape(game) << "\",\n";
        o << "  \"network\": {\n";
        o << "    \"avg_ping_ms\": " << avg_ping << ",\n";
        o << "    \"jitter_ms\": " << jitter << ",\n";
        o << "    \"packet_loss_percent\": " << loss_percent << ",\n";
        o << "    \"quality_score\": " << quality_score << "\n";
        o << "  },\n";
        o << "  \"best_server\": {\"name\": \"" << escape(best_server)
          << "\", \"latency_ms\": " << best_latency << "},\n";
        o << "  \"launch_checks\": [\n";
        for (std::size_t i = 0; i < checks.size(); ++i) {
            o << "    {\"name\": \"" << escape(checks[i].name)
              << "\", \"severity\": " << checks[i].severity << "}"
              << (i + 1 < checks.size() ? "," : "") << "\n";
        }
        o << "  ]\n}\n";
        return o.str();
    }
};

} // namespace gno
