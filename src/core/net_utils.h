#pragma once

// Pure logic behind the extended network utilities (v1.7.0).
// Deterministic parsers, protocol builders/parsers, graders and search
// helpers - all unit-testable. Platform I/O lives in platform_netscan / UI.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace gno {
namespace netutils {

// ---------------------------------------------------------------- MTR-lite

struct HopStats {
    int hop = 0;
    std::string ip;
    int sent = 0;
    int lost = 0;
    double avg_ms = 0.0;
    double jitter_ms = 0.0;

    double lossPercent() const { return sent == 0 ? 100.0 : 100.0 * lost / sent; }
};

// latencies: one entry per probe, negative value == timeout/loss.
inline void summarizeProbes(HopStats& hop, const std::vector<double>& latencies) {
    hop.sent = static_cast<int>(latencies.size());
    hop.lost = 0;
    double sum = 0;
    std::vector<double> ok;
    for (double l : latencies) {
        if (l < 0) { ++hop.lost; continue; }
        sum += l;
        ok.push_back(l);
    }
    hop.avg_ms = ok.empty() ? 0.0 : sum / ok.size();
    if (ok.size() >= 2) {
        double j = 0;
        for (std::size_t i = 1; i < ok.size(); ++i)
            j += std::fabs(ok[i] - ok[i - 1]);
        hop.jitter_ms = j / (ok.size() - 1);
    } else {
        hop.jitter_ms = 0.0;
    }
}

inline const char* hopVerdict(const HopStats& h) {
    if (h.sent > 0 && h.lost == h.sent) return "DEAD";
    if (h.lossPercent() > 15.0 || h.avg_ms > 120.0) return "BAD";
    if (h.lossPercent() > 2.0 || h.jitter_ms > 12.0) return "WARN";
    return "OK";
}

// ---------------------------------------------------------------- Wi-Fi analyzer

struct WifiNetwork {
    std::string ssid;
    std::string bssid;
    int channel = 0;
    int rssi = -100;
};

inline int signalQuality(int rssi) {
    if (rssi <= -90) return 0;
    if (rssi >= -30) return 100;
    return 2 * (rssi + 90);
}

// Parses localized `netsh wlan show networks mode=bssid` output loosely:
// tracks SSID blocks and BSSID/Channel/Signal lines regardless of language.
inline std::vector<WifiNetwork> parseNetshWlan(const std::string& text) {
    std::vector<WifiNetwork> out;
    WifiNetwork cur;
    bool have_ssid = false;

    auto trim = [](std::string s) {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\r' || s.front() == '\t')) s.erase(s.begin());
        while (!s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\t')) s.pop_back();
        return s;
    };
    auto afterColon = [&](const std::string& l) {
        const auto pos = l.find(':');
        return pos == std::string::npos ? std::string() : trim(l.substr(pos + 1));
    };
    auto flush = [&]() {
        if (have_ssid && !cur.bssid.empty()) out.push_back(cur);
    };

    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        const bool is_ssid = line.find("SSID") != std::string::npos &&
                             line.find("BSSID") == std::string::npos;
        if (is_ssid) {
            flush();
            cur = WifiNetwork{};
            have_ssid = true;
            cur.ssid = afterColon(line);
            continue;
        }
        if (!have_ssid) continue;
        if (line.find("BSSID") != std::string::npos) {
            flush();                       // previous BSSID block complete
            cur.bssid.clear();
            cur.channel = 0;
            cur.rssi = -100;
            cur.bssid = afterColon(line);
        } else if (line.find("Channel") != std::string::npos ||
                   line.find("анал") != std::string::npos) { // канал/Канал
            try { cur.channel = std::stoi(afterColon(line)); } catch (...) {}
        } else if (line.find("Signal") != std::string::npos ||
                   line.find("игнал") != std::string::npos) { // Сигнал
            try {
                cur.rssi = -100 + static_cast<int>(std::stod(afterColon(line)) * 0.7);
            } catch (...) {}
        }
    }
    flush();
    return out;
}

// Least-congested standard 2.4 GHz channel given neighbour networks.
inline int bestChannel24(const std::vector<WifiNetwork>& networks, int own_channel) {
    int weight[14] = {};
    for (const auto& n : networks) {
        if (n.channel < 1 || n.channel > 13 || n.channel == own_channel) continue;
        const int strength = signalQuality(n.rssi) + 1;
        for (int ch = 1; ch <= 13; ++ch) {
            const int dist = std::abs(ch - n.channel);
            if (dist <= 2) weight[ch] += strength * (3 - dist);
        }
    }
    int result = own_channel;
    int best_w = 1 << 30;
    for (int ch : {1, 6, 11}) {
        if (weight[ch] < best_w) { best_w = weight[ch]; result = ch; }
    }
    return result;
}

// ---------------------------------------------------------------- STUN (RFC 5389)

inline std::vector<std::uint8_t> buildBindingRequest(const std::uint8_t tid[12]) {
    std::vector<std::uint8_t> m = {0x00, 0x01, 0x00, 0x00, 0x21, 0x12, 0xA4, 0x42};
    for (int i = 0; i < 12; ++i) m.push_back(tid[i]);
    return m;
}

// Parses XOR-MAPPED-ADDRESS (type 0x0020) from a binding response, IPv4 only.
inline bool parseXorMappedAddress(const std::uint8_t* data, std::size_t len,
                                  std::string& out_ip, std::uint16_t& out_port) {
    if (len < 20 || data[0] != 0x01 || data[1] != 0x01) return false;
    const std::size_t msg_len = (data[2] << 8) | data[3];
    std::size_t off = 20;
    const std::size_t end = std::min(len, 20 + static_cast<std::size_t>(msg_len));
    static const std::uint8_t magic[4] = {0x21, 0x12, 0xA4, 0x42};
    while (off + 4 <= end) {
        const std::uint16_t type = static_cast<std::uint16_t>((data[off] << 8) | data[off + 1]);
        const std::uint16_t size = static_cast<std::uint16_t>((data[off + 2] << 8) | data[off + 3]);
        if (type == 0x0020 && size >= 8 && off + 4 + size <= end) {
            const std::uint8_t* v = data + off + 4;
            if (v[1] != 0x01) return false; // family: IPv4 only
            out_port = static_cast<std::uint16_t>(((v[2] ^ magic[0]) << 8) | (v[3] ^ magic[1]));
            std::uint32_t ip = 0;
            for (int i = 0; i < 4; ++i)
                ip = (ip << 8) | static_cast<std::uint32_t>(v[4 + i] ^ magic[i]);
            out_ip = std::to_string((ip >> 24) & 0xFF) + "." +
                     std::to_string((ip >> 16) & 0xFF) + "." +
                     std::to_string((ip >> 8) & 0xFF) + "." +
                     std::to_string(ip & 0xFF);
            return true;
        }
        off += 4 + ((static_cast<std::size_t>(size) + 3) & ~static_cast<std::size_t>(3));
    }
    return false;
}

inline const char* classifyNat(bool same_mapping_from_two_servers) {
    return same_mapping_from_two_servers ? "Cone" : "Symmetric";
}

// ---------------------------------------------------------------- DNS probe

inline std::vector<std::uint8_t> buildDnsQueryA(const std::string& name, std::uint16_t id) {
    std::vector<std::uint8_t> q;
    q.push_back(static_cast<std::uint8_t>(id >> 8));
    q.push_back(static_cast<std::uint8_t>(id & 0xFF));
    q.push_back(0x01); q.push_back(0x00); // RD
    q.push_back(0); q.push_back(1);       // QDCOUNT = 1
    q.insert(q.end(), 6, 0);              // AN/NS/ARCOUNT = 0
    std::string part;
    for (char c : name) {
        if (c == '.') {
            if (!part.empty()) {
                q.push_back(static_cast<std::uint8_t>(part.size()));
                q.insert(q.end(), part.begin(), part.end());
                part.clear();
            }
        } else part += c;
    }
    if (!part.empty()) {
        q.push_back(static_cast<std::uint8_t>(part.size()));
        q.insert(q.end(), part.begin(), part.end());
    }
    q.push_back(0);
    q.push_back(0); q.push_back(1); // TYPE A
    q.push_back(0); q.push_back(1); // CLASS IN
    return q;
}

inline std::vector<std::string> parseDnsARecords(const std::uint8_t* d, std::size_t len) {
    std::vector<std::string> out;
    if (len < 12) return out;
    const std::size_t qd = (d[4] << 8) | d[5];
    const std::size_t an = (d[6] << 8) | d[7];
    std::size_t off = 12;
    auto skip_name = [&](std::size_t& o) -> bool {
        while (o < len) {
            const std::uint8_t l = d[o];
            if (l == 0) { ++o; return true; }
            if ((l & 0xC0) == 0xC0) { o += 2; return true; }
            o += 1 + l;
        }
        return false;
    };
    for (std::size_t i = 0; i < qd && off < len; ++i) {
        if (!skip_name(off)) return out;
        off += 4;
    }
    for (std::size_t i = 0; i < an && off + 10 <= len; ++i) {
        if (!skip_name(off)) break;
        if (off + 10 > len) break;
        const std::uint16_t type = static_cast<std::uint16_t>((d[off] << 8) | d[off + 1]);
        const std::uint16_t rdl = static_cast<std::uint16_t>((d[off + 8] << 8) | d[off + 9]);
        off += 10;
        if (off + rdl > len) break;
        if (type == 1 && rdl == 4)
            out.push_back(std::to_string(d[off]) + "." + std::to_string(d[off + 1]) + "." +
                          std::to_string(d[off + 2]) + "." + std::to_string(d[off + 3]));
        off += rdl;
    }
    return out;
}

// ---------------------------------------------------------------- MTU path probe

inline int nextMtuProbe(int& lo_ok, int& hi_fail) {
    if (hi_fail - lo_ok <= 1) return lo_ok;
    return (lo_ok + hi_fail) / 2;
}

// ---------------------------------------------------------------- Bufferbloat

struct BloatGrade {
    double delta_percent = 0.0;
    const char* tag = "";
};

inline BloatGrade gradeBufferbloat(double idle_avg, double loaded_avg) {
    if (idle_avg <= 0 || loaded_avg <= 0) return {0.0, "N/A"};
    const double delta = 100.0 * (loaded_avg - idle_avg) / idle_avg;
    if (delta < 20) return {delta, "OK"};
    if (delta < 50) return {delta, "MED"};
    return {delta, "BAD"};
}

} // namespace netutils
} // namespace gno
