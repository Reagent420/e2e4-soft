#include "packet_capture.h"
#include <thread>
#include <chrono>
#include <algorithm>

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

struct ip_header {
    uint8_t ip_hl:4;
    uint8_t ip_v:4;
    uint8_t ip_tos;
    uint16_t ip_len;
    uint16_t ip_id;
    uint16_t ip_off;
    uint8_t ip_ttl;
    uint8_t ip_p;
    uint16_t ip_sum;
    struct in_addr ip_src;
    struct in_addr ip_dst;
};
#endif

namespace gno {

struct PacketCapture::Impl {
#ifdef PLATFORM_WINDOWS
    SOCKET capture_socket = INVALID_SOCKET;
#endif
    bool initialized = false;
    std::thread capture_thread;
    std::atomic<bool> capturing{false};
    
    Impl() {
#ifdef PLATFORM_WINDOWS
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0) {
            capture_socket = socket(AF_INET, SOCK_RAW, IPPROTO_IP);
            if (capture_socket != INVALID_SOCKET) {
                int optval = 1;
                setsockopt(capture_socket, IPPROTO_IP, IP_HDRINCL, (char*)&optval, sizeof(optval));
                initialized = true;
            }
        }
#endif
    }
    
    ~Impl() {
#ifdef PLATFORM_WINDOWS
        if (capture_socket != INVALID_SOCKET) {
            closesocket(capture_socket);
        }
#endif
    }
};

PacketCapture::PacketCapture() : impl_(std::make_unique<Impl>()) {
    game_port_ranges_ = {
        {27015, 27030, "Valve"},
        {3478, 3480, "PlayStation"},
        {3074, 3074, "Xbox"},
        {88, 88, "Xbox Live"},
        {5000, 5000, "Ubisoft"},
        {1024, 65535, "Dynamic"}
    };
}

PacketCapture::~PacketCapture() {
    stopCapture();
    close();
}

bool PacketCapture::open(const std::string& interface_name) {
    return impl_->initialized;
}

void PacketCapture::close() {
    stopCapture();
}

bool PacketCapture::isOpen() const {
    return impl_->initialized;
}

void PacketCapture::setFilter(const std::string& filter_expression) {
}

void PacketCapture::setPacketCallback(PacketCallback callback) {
    packet_callback_ = std::move(callback);
}

void PacketCapture::startCapture() {
    if (impl_->capturing) return;
    impl_->capturing = true;
    capturing_ = true;
    impl_->capture_thread = std::thread(&PacketCapture::captureLoop, this);
}

void PacketCapture::stopCapture() {
    impl_->capturing = false;
    capturing_ = false;
    if (impl_->capture_thread.joinable()) {
        impl_->capture_thread.join();
    }
}

bool PacketCapture::isCapturing() const {
    return impl_->capturing;
}

void PacketCapture::addGamePortRange(const GamePortRange& range) {
    game_port_ranges_.push_back(range);
}

void PacketCapture::removeGamePortRange(uint16_t start_port) {
    game_port_ranges_.erase(
        std::remove_if(game_port_ranges_.begin(), game_port_ranges_.end(),
                       [start_port](const GamePortRange& r) { return r.start_port == start_port; }),
        game_port_ranges_.end());
}

std::vector<GamePortRange> PacketCapture::getGamePortRanges() const {
    return game_port_ranges_;
}

bool PacketCapture::isGamePort(uint16_t port, const std::vector<GamePortRange>& ranges) {
    for (const auto& range : ranges) {
        if (port >= range.start_port && port <= range.end_port) {
            return true;
        }
    }
    return false;
}

void PacketCapture::captureLoop() {
#ifdef PLATFORM_WINDOWS
    char buffer[65535];
    
    while (impl_->capturing) {
        struct sockaddr_in from;
        int from_len = sizeof(from);
        
        int bytes = recvfrom(impl_->capture_socket, buffer, sizeof(buffer), 0,
                            (struct sockaddr*)&from, &from_len);
        
        if (bytes > 0) {
            CapturedPacket packet;
            packet.timestamp = std::chrono::steady_clock::now();
            packet.size = bytes;
            
            struct ip_header* ip = (struct ip_header*)buffer;
            
            char src_str[INET_ADDRSTRLEN] = {0};
            char dst_str[INET_ADDRSTRLEN] = {0};
            inet_ntop(AF_INET, &ip->ip_src, src_str, sizeof(src_str));
            inet_ntop(AF_INET, &ip->ip_dst, dst_str, sizeof(dst_str));
            
            packet.source_ip = src_str;
            packet.dest_ip = dst_str;
            packet.protocol = ip->ip_p;
            packet.is_game_traffic = isGameTraffic(packet);
            
            if (packet_callback_) {
                packet_callback_(packet);
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
#endif
}

bool PacketCapture::isGameTraffic(const CapturedPacket& packet) const {
    return isGamePort(packet.dest_port, game_port_ranges_) ||
           isGamePort(packet.source_port, game_port_ranges_);
}

} // namespace gno
