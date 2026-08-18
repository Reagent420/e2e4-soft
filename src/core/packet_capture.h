#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <memory>
#include <chrono>
#include <atomic>
#include <thread>

namespace gno {

struct CapturedPacket {
    std::chrono::steady_clock::time_point timestamp;
    std::string source_ip;
    std::string dest_ip;
    uint16_t source_port = 0;
    uint16_t dest_port = 0;
    uint32_t protocol = 0;
    uint32_t size = 0;
    std::vector<uint8_t> payload;
    bool is_game_traffic = false;
};

struct GamePortRange {
    uint16_t start_port;
    uint16_t end_port;
    std::string protocol_name;
};

class PacketCapture {
public:
    PacketCapture();
    ~PacketCapture();

    bool open(const std::string& interface_name);
    void close();
    bool isOpen() const;

    void setFilter(const std::string& filter_expression);
    
    using PacketCallback = std::function<void(const CapturedPacket&)>;
    void setPacketCallback(PacketCallback callback);

    void startCapture();
    void stopCapture();
    bool isCapturing() const;

    void addGamePortRange(const GamePortRange& range);
    void removeGamePortRange(uint16_t start_port);
    std::vector<GamePortRange> getGamePortRanges() const;

    static bool isGamePort(uint16_t port, const std::vector<GamePortRange>& ranges);

private:
    void captureLoop();
    bool isGameTraffic(const CapturedPacket& packet) const;

    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::vector<GamePortRange> game_port_ranges_;
    std::atomic<bool> capturing_{false};
    PacketCallback packet_callback_;
};

} // namespace gno
