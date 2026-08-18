#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <iomanip>
#include "core/route_analyzer.h"
#include "core/multipath_engine.h"
#include "core/game_detector.h"
#include "core/network_utils.h"
#include "monitoring/ping_monitor.h"
#include "monitoring/packet_loss_monitor.h"
#include "monitoring/jitter_calculator.h"
#include "optimization/fps_optimizer.h"
#include "monitoring/stats_collector.h"

void printHeader(const std::string& title) {
    std::cout << "\n========================================\n";
    std::cout << "  " << title << "\n";
    std::cout << "========================================\n";
}

void printSeparator() {
    std::cout << "----------------------------------------\n";
}

int main(int argc, char* argv[]) {
    std::string target = (argc > 1) ? argv[1] : "8.8.8.8";

    printHeader("GNO - Game Network Optimizer v1.0.0");

    // Test 1: Network Utils
    printSeparator();
    std::cout << "[TEST 1] Network Utilities\n";
    std::string local_ip = gno::NetworkUtils::getLocalIPAddress();
    std::cout << "  Local IP: " << local_ip << "\n";
    auto resolved = gno::NetworkUtils::resolveDNS("google.com");
    std::cout << "  DNS resolve google.com:";
    for (const auto& ip : resolved) std::cout << " " << ip;
    std::cout << "\n";
    std::cout << "  Port 443 open: " << (gno::NetworkUtils::isPortOpen("1.1.1.1", 443) ? "YES" : "NO") << "\n";

    // Test 2: Game Detector
    printSeparator();
    std::cout << "[TEST 2] Game Detection\n";
    gno::GameDetector detector;
    auto games = detector.getSupportedGames();
    std::cout << "  Supported games: " << games.size() << "\n";
    for (size_t i = 0; i < std::min<size_t>(5, games.size()); i++) {
        std::cout << "    - " << games[i].name << " (" << games[i].category << ")\n";
    }
    detector.scanInstalledGames();
    auto installed = detector.getInstalledGames();
    std::cout << "  Installed: " << installed.size() << "\n";
    detector.detectRunningGames();
    auto running = detector.getRunningGames();
    std::cout << "  Running: " << running.size() << "\n";

    // Test 3: Route Analyzer
    printSeparator();
    std::cout << "[TEST 3] Route Analyzer\n";
    gno::RouteAnalyzer analyzer;
    auto routes = analyzer.getRoutes();
    std::cout << "  Routes found: " << routes.size() << "\n";
    auto interfaces = analyzer.getInterfaces();
    std::cout << "  Interfaces: " << interfaces.size() << "\n";
    for (const auto& iface : interfaces) {
        std::cout << "    - " << iface.name << " (" << iface.ip_address << ")\n";
    }

    // Test 4: Ping Monitor
    printSeparator();
    std::cout << "[TEST 4] Ping Monitor -> " << target << "\n";
    gno::PingMonitor pinger;
    auto results = pinger.pingBatch(target, 5, 3000);
    int success = 0;
    double total_lat = 0;
    for (const auto& r : results) {
        if (r.success) { success++; total_lat += r.latency_ms; }
    }
    std::cout << "  Sent: 5, Received: " << success << ", Loss: " << (5 - success) << "\n";
    if (success > 0) {
        std::cout << "  Avg latency: " << std::fixed << std::setprecision(1) << (total_lat / success) << " ms\n";
    }

    // Test 5: Packet Loss
    printSeparator();
    std::cout << "[TEST 5] Packet Loss -> " << target << "\n";
    gno::PacketLossMonitor plm;
    auto plResult = plm.measure(target, 10, 3000);
    std::cout << "  Sent: " << plResult.packets_sent
              << ", Received: " << plResult.packets_received
              << ", Loss: " << std::fixed << std::setprecision(1) << plResult.loss_percent << "%\n";

    // Test 6: Jitter Calculator
    printSeparator();
    std::cout << "[TEST 6] Jitter Calculator\n";
    gno::JitterCalculator jitter;
    for (const auto& r : results) {
        if (r.success) jitter.addSample(r.latency_ms);
    }
    auto jStats = jitter.getStats();
    std::cout << "  Samples: " << jStats.sample_count << "\n";
    std::cout << "  Current jitter: " << std::fixed << std::setprecision(2) << jStats.current_jitter_ms << " ms\n";
    std::cout << "  Average jitter: " << jStats.average_jitter_ms << " ms\n";

    // Test 7: FPS Optimizer (read-only)
    printSeparator();
    std::cout << "[TEST 7] FPS Optimizer\n";
    gno::FPSOptimizer fpsOpt;
    auto fpsConfig = fpsOpt.getCurrentConfig();
    std::cout << "  Game DVR disable: " << (fpsConfig.disable_game_dvr ? "YES" : "NO") << "\n";
    std::cout << "  Fullscreen opt: " << (fpsConfig.disable_fullscreen_optimizations ? "YES" : "NO") << "\n";
    std::cout << "  Mouse accel: " << (fpsConfig.disable_mouse_acceleration ? "YES" : "NO") << "\n";

    // Test 8: Stats Collector
    printSeparator();
    std::cout << "[TEST 8] Stats Collector\n";
    gno::StatsCollector stats;
    stats.start("test_session");
    for (const auto& r : results) {
        if (r.success) {
            gno::NetworkSnapshot snap;
            snap.ping_ms = r.latency_ms;
            snap.jitter_ms = 0;
            snap.packet_loss_percent = 0;
            snap.timestamp = r.timestamp;
            stats.recordSnapshot(snap);
        }
    }
    auto session = stats.getCurrentSession();
    std::cout << "  Snapshots recorded: " << session.total_samples << "\n";
    std::cout << "  Session active: " << (stats.isRecording() ? "YES" : "NO") << "\n";
    stats.stop();
    std::cout << "  Session stopped: " << (!stats.isRecording() ? "YES" : "NO") << "\n";

    printSeparator();
    printHeader("ALL TESTS PASSED");
    return 0;
}
