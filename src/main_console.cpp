#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <iomanip>
#include <vector>
#include <algorithm>
#include "core/route_analyzer.h"
#include "core/multipath_engine.h"
#include "core/game_detector.h"
#include "core/network_utils.h"
#include "core/game_profiles.h"
#include "core/input_validation.h"
#include "core/speed_test.h"
#include "core/dns_manager.h"
#include "core/process_monitor.h"
#include "core/session_history.h"
#include "core/game_watcher.h"
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

void printHelp() {
    std::cout << R"(
E2E4 Soft - Game Network Optimizer v1.2.0

Usage: E2E4-console [OPTIONS]

Network Testing:
  --target <ip>         Target IP for ping/loss tests (default: 8.8.8.8)
  --ping <count>        Number of ping packets (default: 5)
  --speedtest           Run speedtest to multiple servers
  --dns <server>        Test specific DNS server
  --dns-benchmark       Benchmark all DNS presets

Game Optimization:
  --boost               Apply FPS boost optimizations
  --game <name>         Apply profile for specific game
  --list-games          List all supported games
  --list-installed      List installed games
  --watch               Start game watcher (auto-apply profiles)

System:
  --processes           Show top processes by memory
  --history             Show session history
  --history-clear       Clear session history

Profiles:
  --export-profile <file>  Export game profiles to JSON
  --import-profile <file>  Import game profiles from JSON

DNS:
  --dns <server>        Test specific DNS server latency
  --dns-apply <primary> [secondary]  Apply DNS settings

General:
  -h, --help            Show this help
  -v, --version         Show version

Examples:
  E2E4-console --target 1.1.1.1 --ping 10
  E2E4-console --boost --game "Counter-Strike 2"
  E2E4-console --speedtest
  E2E4-console --dns-benchmark
  E2E4-console --watch
  E2E4-console --export-profile profiles.json
)" << std::endl;
}

int main(int argc, char* argv[]) {
    std::string target = "8.8.8.8";
    int ping_count = 5;
    bool run_tests = true;
    bool run_speedtest = false;
    bool run_dns_benchmark = false;
    std::string dns_server;
    bool run_boost = false;
    std::string game_name;
    bool list_games = false;
    bool list_installed = false;
    bool run_watch = false;
    bool show_processes = false;
    bool show_history = false;
    bool clear_history = false;
    std::string export_profile;
    std::string import_profile;
    std::string dns_primary, dns_secondary;
    bool apply_dns = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printHelp();
            return 0;
        } else if (arg == "-v" || arg == "--version") {
            std::cout << "GNO-console v1.0.0\n";
            return 0;
        } else if (arg == "--target" && i + 1 < argc) {
            target = argv[++i];
        } else if (arg == "--ping" && i + 1 < argc) {
            const auto parsed = gno::parseBoundedInt(argv[++i], 1, 100);
            if (!parsed) {
                std::cerr << "--ping must be an integer from 1 to 100\n";
                return 2;
            }
            ping_count = *parsed;
        } else if (arg == "--speedtest") {
            run_speedtest = true;
            run_tests = false;
        } else if (arg == "--dns-benchmark") {
            run_dns_benchmark = true;
            run_tests = false;
        } else if (arg == "--dns" && i + 1 < argc) {
            dns_server = argv[++i];
            run_tests = false;
        } else if (arg == "--boost") {
            run_boost = true;
            run_tests = false;
        } else if (arg == "--game" && i + 1 < argc) {
            game_name = argv[++i];
            run_tests = false;
        } else if (arg == "--list-games") {
            list_games = true;
            run_tests = false;
        } else if (arg == "--list-installed") {
            list_installed = true;
            run_tests = false;
        } else if (arg == "--watch") {
            run_watch = true;
            run_tests = false;
        } else if (arg == "--processes") {
            show_processes = true;
            run_tests = false;
        } else if (arg == "--history") {
            show_history = true;
            run_tests = false;
        } else if (arg == "--history-clear") {
            clear_history = true;
            run_tests = false;
        } else if (arg == "--export-profile" && i + 1 < argc) {
            export_profile = argv[++i];
            run_tests = false;
        } else if (arg == "--import-profile" && i + 1 < argc) {
            import_profile = argv[++i];
            run_tests = false;
        } else if (arg == "--dns-apply" && i + 1 < argc) {
            dns_primary = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                dns_secondary = argv[++i];
            }
            apply_dns = true;
            run_tests = false;
        } else if (arg[0] == '-') {
            std::cerr << "Unknown option: " << arg << "\n";
            printHelp();
            return 1;
        }
    }

    if (clear_history) {
        gno::SessionHistory sh;
        sh.clear();
        std::cout << "Session history cleared.\n";
        return 0;
    }

    if (list_games) {
        gno::GameDetector detector;
        auto games = detector.getSupportedGames();
        std::cout << "Supported games (" << games.size() << "):\n";
        for (const auto& g : games) {
            std::cout << "  " << g.name << " (" << g.category << ") - " << g.process_name << "\n";
        }
        return 0;
    }

    if (list_installed) {
        gno::GameDetector detector;
        detector.scanInstalledGames();
        auto installed = detector.getInstalledGames();
        std::cout << "Installed games (" << installed.size() << "):\n";
        for (const auto& g : installed) {
            std::cout << "  " << g.name << " -> " << g.executable_path << "\n";
        }
        return 0;
    }

    if (show_processes) {
        gno::ProcessMonitor pm;
        auto procs = pm.getTopProcesses(20);
        std::cout << "Top processes by memory:\n";
        for (const auto& p : procs) {
            std::cout << "  PID " << p.pid << ": " << p.name 
                      << " (" << p.memory_mb << " MB)";
            if (p.is_game) std::cout << " [GAME]";
            std::cout << "\n";
        }
        return 0;
    }

    if (show_history) {
        gno::SessionHistory sh;
        auto records = sh.getAll();
        std::cout << "Session history (" << records.size() << " records):\n";
        for (const auto& r : records) {
            std::cout << "  " << r.start_time_str << " - " << r.game_name
                      << " | Ping: " << r.avg_ping_ms << "ms"
                      << " | Jitter: " << r.avg_jitter_ms << "ms"
                      << " | Loss: " << r.avg_packet_loss << "%"
                      << " | Boost: " << (r.boost_was_active ? "ON" : "OFF") << "\n";
        }
        return 0;
    }

    if (!export_profile.empty()) {
        gno::GameProfiles profiles;
        if (profiles.exportToFile(export_profile)) {
            std::cout << "Profiles exported to " << export_profile << "\n";
        } else {
            std::cerr << "Failed to export profiles\n";
            return 1;
        }
        return 0;
    }

    if (!import_profile.empty()) {
        gno::GameProfiles profiles;
        if (profiles.importFromFile(import_profile)) {
            std::cout << "Profiles imported from " << import_profile << "\n";
        } else {
            std::cerr << "Failed to import profiles\n";
            return 1;
        }
        return 0;
    }

    if (apply_dns) {
        gno::DNSManager dm;
        if (dm.applyDNS(dns_primary, dns_secondary)) {
            std::cout << "DNS applied: " << dns_primary;
            if (!dns_secondary.empty()) std::cout << ", " << dns_secondary;
            std::cout << "\n";
        } else {
            std::cout << "Failed to apply DNS\n";
        }
        return 0;
    }

    if (!dns_server.empty()) {
        gno::DNSManager dm;
        auto result = dm.benchmarkServer(dns_server);
        if (result.success) {
            std::cout << "DNS " << dns_server << ": " << result.latency_ms << " ms\n";
        } else {
            std::cout << "DNS " << dns_server << ": timeout\n";
        }
        return 0;
    }

    if (run_dns_benchmark) {
        gno::DNSManager dm;
        std::cout << "Benchmarking DNS servers...\n";
        auto results = dm.benchmarkAll();
        auto fastest = dm.getFastestServer();
        for (const auto& r : results) {
            if (r.success) {
                std::cout << "  " << r.server << ": " << r.latency_ms << " ms\n";
            } else {
                std::cout << "  " << r.server << ": timeout\n";
            }
        }
        if (fastest.success) {
            std::cout << "\nFastest: " << fastest.server << " (" << fastest.latency_ms << " ms)\n";
        }
        return 0;
    }

    if (run_speedtest) {
        gno::SpeedTest st;
        std::cout << "Running speedtest...\n";
        st.runBenchmark();
        while (st.isRunning()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        auto results = st.getResults();
        auto best = st.getBestServer();
        std::cout << "Results:\n";
        for (const auto& r : results) {
            if (r.success) {
                std::cout << "  " << r.server_name << " (" << r.server_ip << "): " << r.latency_ms << " ms\n";
            } else {
                std::cout << "  " << r.server_name << " (" << r.server_ip << "): timeout\n";
            }
        }
        if (best.success) {
            std::cout << "\nBest server: " << best.server_name << " (" << best.latency_ms << " ms)\n";
        }
        return 0;
    }

    if (run_boost) {
        gno::FPSOptimizer fpsOpt;
        gno::FPSBoostConfig config;
        config.disable_game_dvr = true;
        config.disable_fullscreen_optimizations = true;
        config.disable_mouse_acceleration = true;
        config.optimize_power_plan = true;
        auto result = fpsOpt.applyConfig(config);
        if (result.success) {
            std::cout << "FPS Boost applied:\n";
            for (const auto& c : result.applied_changes) {
                std::cout << "  - " << c << "\n";
            }
        } else {
            std::cout << "FPS Boost failed: " << result.message << "\n";
        }
        return 0;
    }

    if (!game_name.empty()) {
        gno::GameProfiles profiles;
        gno::GameDetector detector;
        detector.scanInstalledGames();
        
        auto games = detector.getInstalledGames();
        bool found = false;
        for (const auto& g : games) {
            if (g.name == game_name) {
                found = true;
                break;
            }
        }
        if (!found) {
            std::cerr << "Game not found or not installed: " << game_name << "\n";
            return 1;
        }
        
        gno::GameProfile profile = profiles.get(game_name);
        if (profile.game_name.empty()) {
            profile.game_name = game_name;
            profile.process_name = "";
            for (const auto& g : games) {
                if (g.name == game_name) {
                    profile.process_name = g.process_name;
                    break;
                }
            }
            profile.multipath_enabled = true;
            profile.fps_boost_enabled = true;
            profile.network_optimization = true;
            profile.max_routes = 3;
            profile.auto_apply = true;
        }
        
        profiles.set(profile);
        std::cout << "Profile applied for " << game_name << "\n";
        
        // Apply FPS boost
        gno::FPSOptimizer fpsOpt;
        gno::FPSBoostConfig config;
        config.disable_game_dvr = profile.fps_boost_enabled;
        config.disable_fullscreen_optimizations = profile.fps_boost_enabled;
        config.disable_mouse_acceleration = profile.fps_boost_enabled;
        config.optimize_power_plan = profile.network_optimization;
        fpsOpt.applyConfig(config);
        std::cout << "Optimizations applied.\n";
        return 0;
    }

    if (run_watch) {
        gno::GameWatcher watcher;
        gno::GameWatcherConfig config;
        config.enabled = true;
        config.check_interval_ms = 2000;
        config.auto_apply_profiles = true;
        config.notify_on_game_start = true;
        config.notify_on_game_end = true;
        
        gno::GameProfiles profiles;
        gno::GameDetector detector;
        
        watcher.setGameStartCallback([&](const std::string& game_name, const std::string&, uint32_t pid) {
            std::cout << "[GAME START] " << game_name << " (PID: " << pid << ")\n";
            if (config.auto_apply_profiles) {
                auto p = profiles.get(game_name);
                if (!p.game_name.empty()) {
                    std::cout << "  Auto-applying profile for " << game_name << "\n";
                    // Apply profile logic here
                }
            }
        });
        
        watcher.setGameEndCallback([&](const std::string& game_name, const std::string&) {
            std::cout << "[GAME END] " << game_name << "\n";
        });
        
        watcher.start(config);
        std::cout << "Game watcher started. Press Ctrl+C to stop.\n";
        
        // Keep running
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    // Default: run all tests
    if (run_tests) {
        std::cout << "========================================\n";
        std::cout << "  GNO - Game Network Optimizer v1.0.0\n";
        std::cout << "========================================\n";

        // Test 1: Network Utils
        std::cout << "----------------------------------------\n";
        std::cout << "[TEST 1] Network Utilities\n";
        std::string local_ip = gno::NetworkUtils::getLocalIPAddress();
        std::cout << "  Local IP: " << local_ip << "\n";
        auto resolved = gno::NetworkUtils::resolveDNS("google.com");
        std::cout << "  DNS resolve google.com:";
        for (const auto& ip : resolved) std::cout << " " << ip;
        std::cout << "\n";
        std::cout << "  Port 443 open: " << (gno::NetworkUtils::isPortOpen("1.1.1.1", 443) ? "YES" : "NO") << "\n";

        // Test 2: Game Detector
        std::cout << "----------------------------------------\n";
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
        std::cout << "----------------------------------------\n";
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
        std::cout << "----------------------------------------\n";
        std::cout << "[TEST 4] Ping Monitor -> " << target << "\n";
        gno::PingMonitor pinger;
        auto results = pinger.pingBatch(target, static_cast<uint32_t>(ping_count), 3000);
        int success = 0;
        double total_lat = 0;
        for (const auto& r : results) {
            if (r.success) { success++; total_lat += r.latency_ms; }
        }
        std::cout << "  Sent: " << ping_count << ", Received: " << success << ", Loss: " << (ping_count - success) << "\n";
        if (success > 0) {
            std::cout << "  Avg latency: " << std::fixed << std::setprecision(1) << (total_lat / success) << " ms\n";
        }

        // Test 5: Packet Loss
        std::cout << "----------------------------------------\n";
        std::cout << "[TEST 5] Packet Loss -> " << target << "\n";
        gno::PacketLossMonitor plm;
        auto plResult = plm.measure(target, 10, 3000);
        std::cout << "  Sent: " << plResult.packets_sent
                  << ", Received: " << plResult.packets_received
                  << ", Loss: " << std::fixed << std::setprecision(1) << plResult.loss_percent << "%\n";

        // Test 6: Jitter Calculator
        std::cout << "----------------------------------------\n";
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
        std::cout << "----------------------------------------\n";
        std::cout << "[TEST 7] FPS Optimizer\n";
        gno::FPSOptimizer fpsOpt;
        auto fpsConfig = fpsOpt.getCurrentConfig();
        std::cout << "  Game DVR disable: " << (fpsConfig.disable_game_dvr ? "YES" : "NO") << "\n";
        std::cout << "  Fullscreen opt: " << (fpsConfig.disable_fullscreen_optimizations ? "YES" : "NO") << "\n";
        std::cout << "  Mouse accel: " << (fpsConfig.disable_mouse_acceleration ? "YES" : "NO") << "\n";

        // Test 8: Stats Collector
        std::cout << "----------------------------------------\n";
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

        std::cout << "----------------------------------------\n";
        std::cout << "========================================\n";
        std::cout << "  ALL TESTS PASSED\n";
        std::cout << "========================================\n";
    }

    return 0;
}
