#pragma once

// Pure-logic model behind the server map page: real server nodes, region
// filtering, latency grading and best-server selection. No Qt, unit-testable.

#include <algorithm>
#include <string>
#include <vector>

namespace gno {

struct MapServer {
    std::string name;
    std::string city;
    std::string country;
    std::string ip;
    double latitude = 0.0;
    double longitude = 0.0;
    int latency_ms = -1; // -1 unknown, -2 offline
    std::int64_t last_probe_ms = 0;
};

enum class MapGrade { Unknown, Offline, Good, Medium, Bad };

class ServerMapModel {
public:
    static MapGrade grade(int latency_ms) {
        if (latency_ms == -2) return MapGrade::Offline; // probed, no reply
        if (latency_ms < 0) return MapGrade::Unknown;
        if (latency_ms <= 40) return MapGrade::Good;
        if (latency_ms <= 100) return MapGrade::Medium;
        return MapGrade::Bad;
    }

    // Regions: "all", "EU", "NA", "ASIA", "SA", "OCE"
    static std::string regionOf(const std::string& country) {
        static const char* eu[] = {"Russia", "Germany", "UK", "Netherlands", "France",
                                   "Poland", "Sweden", "Ukraine", "Spain", "Italy", "Turkey"};
        static const char* na[] = {"USA", "Canada", "Mexico"};
        static const char* asia[] = {"Japan", "Singapore", "China", "Korea", "Hong Kong", "India"};
        static const char* sa[] = {"Brazil", "Argentina", "Chile"};
        static const char* oce[] = {"Australia", "New Zealand"};
        auto in = [&](const char** list, int n) {
            for (int i = 0; i < n; ++i)
                if (country == list[i]) return true;
            return false;
        };
        if (in(eu, 10)) return "EU";
        if (in(na, 3)) return "NA";
        if (in(asia, 6)) return "ASIA";
        if (in(sa, 3)) return "SA";
        if (in(oce, 2)) return "OCE";
        return "OTHER";
    }

    static std::vector<MapServer> filterByRegion(const std::vector<MapServer>& servers,
                                                 const std::string& region) {
        if (region == "all") return servers;
        std::vector<MapServer> out;
        for (const auto& s : servers)
            if (regionOf(s.country) == region) out.push_back(s);
        return out;
    }

    // Merge probe results produced on a worker thread (index -> latency or -2).
    static void applyProbeResults(std::vector<MapServer>& servers,
                                  const std::vector<std::pair<int, int>>& results) {
        for (const auto& [index, latency] : results) {
            if (index >= 0 && index < static_cast<int>(servers.size()))
                servers[static_cast<std::size_t>(index)].latency_ms = latency;
        }
    }
    // Index of the lowest-latency measured server, or -1.
    static int bestServer(const std::vector<MapServer>& servers) {
        int best = -1;
        for (std::size_t i = 0; i < servers.size(); ++i) {
            const int l = servers[i].latency_ms;
            if (l < 0) continue;
            if (best == -1 || l < servers[static_cast<std::size_t>(best)].latency_ms)
                best = static_cast<int>(i);
        }
        return best;
    }
};

} // namespace gno
