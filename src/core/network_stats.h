#pragma once

// Reusable descriptive statistics (port of the C# Gno.NetworkEngine.Diagnostics).

#include <algorithm>
#include <cmath>
#include <vector>

namespace gno {

struct NetworkStats {
    double mean = 0;
    double median = 0;
    double p95 = 0;
    double stddev = 0;
    double min = 0;
    double max = 0;
    std::size_t count = 0;
};

class NetworkStatistics {
public:
    static NetworkStats compute(std::vector<double> values) {
        NetworkStats s;
        s.count = values.size();
        if (values.empty()) return s;

        std::sort(values.begin(), values.end());
        s.min = values.front();
        s.max = values.back();

        double sum = 0;
        for (double v : values) sum += v;
        s.mean = sum / static_cast<double>(values.size());

        const std::size_t mid = values.size() / 2;
        s.median = values.size() % 2 == 0 ? (values[mid - 1] + values[mid]) / 2.0 : values[mid];

        const std::size_t rank = static_cast<std::size_t>(
            std::ceil(0.95 * static_cast<double>(values.size())));
        s.p95 = values[std::min(values.size() - 1, rank == 0 ? 0 : rank - 1)];

        if (values.size() >= 2) {
            double variance = 0;
            for (double v : values) variance += (v - s.mean) * (v - s.mean);
            variance /= static_cast<double>(values.size() - 1);
            s.stddev = std::sqrt(variance);
        }
        return s;
    }

    static double packetLossPercent(int total, int received) {
        if (total == 0) return 0;
        return std::round((1.0 - static_cast<double>(received) / total) * 100.0 * 100.0) / 100.0;
    }
};

} // namespace gno
