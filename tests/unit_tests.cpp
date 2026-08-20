#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "core/game_detector.h"
#include "core/route_analyzer.h"
#include "core/game_profiles.h"
#include "core/speed_test.h"
#include "core/dns_manager.h"
#include "core/process_monitor.h"
#include "core/game_watcher.h"
#include "monitoring/jitter_calculator.h"
#include "monitoring/packet_loss_monitor.h"
#include "monitoring/stats_collector.h"
#include "monitoring/ping_monitor.h"
#include "diagnostics/diagnostic_types.h"

#include <chrono>
#include <atomic>
#include <filesystem>
#include <functional>
#include <thread>
#include <type_traits>

namespace {

template <typename T, typename = void>
struct HasBenchmarkCallbackType : std::false_type {};

template <typename T>
struct HasBenchmarkCallbackType<T, std::void_t<typename T::BenchmarkCallback>> : std::true_type {};

template <typename T, typename = void>
struct HasBenchmarkCallbackSetter : std::false_type {};

template <typename T>
struct HasBenchmarkCallbackSetter<T, std::void_t<decltype(&T::setBenchmarkCallback)>> : std::true_type {};

template <typename T, typename = void>
struct HasMeasurementError : std::false_type {};

template <typename T>
struct HasMeasurementError<T, std::void_t<decltype(std::declval<T>().error)>> : std::true_type {};

template <typename T>
gno::DiagnosticError measurementError(const T& result) {
    if constexpr (HasMeasurementError<T>::value) {
        return result.error;
    }
    return gno::DiagnosticError::InternalFailure;
}

template <typename T, typename = void>
struct HasSupportQuery : std::false_type {};

template <typename T>
struct HasSupportQuery<T, std::void_t<decltype(T::isSupported())>> : std::true_type {};

template <typename T>
bool isMeasurementSupported() {
    if constexpr (HasSupportQuery<T>::value) {
        return T::isSupported();
    }
    return true;
}

template <typename Predicate>
bool waitUntil(Predicate&& predicate, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}

} // namespace

using namespace gno;

TEST_CASE("GameDetector::supported games") {
    GameDetector detector;
    auto games = detector.getSupportedGames();
    REQUIRE(games.size() == 23);
    bool hasCS2 = false;
    for (const auto& g : games) {
        if (g.name == "Counter-Strike 2") hasCS2 = true;
    }
    REQUIRE(hasCS2);
}

TEST_CASE("GameDetector::regions") {
    GameDetector detector;
    auto regions = detector.getRegionsForGame("Counter-Strike 2");
    REQUIRE(regions.size() >= 5);
}

TEST_CASE("RouteAnalyzer::getRoutes") {
    RouteAnalyzer analyzer;
    auto routes = analyzer.getRoutes();
#ifdef PLATFORM_WINDOWS
    REQUIRE(routes.size() > 0);
    bool hasValid = false;
    for (const auto& r : routes) {
        if (!r.interface_name.empty() || r.gateway_ip != "0.0.0.0") {
            hasValid = true;
            break;
        }
    }
    REQUIRE(hasValid);
#else
    REQUIRE(routes.empty());
#endif
}

TEST_CASE("GameProfiles::save/load") {
    const auto storage_root = std::filesystem::path("unit-game-profiles-storage");
    std::error_code error;
    std::filesystem::remove_all(storage_root, error);
    GameProfiles profiles(storage_root);
    profiles.remove("TestProfileGame");
    
    GameProfile p;
    p.game_name = "TestProfileGame";
    p.process_name = "testprofile.exe";
    p.multipath_enabled = true;
    p.fps_boost_enabled = false;
    p.network_optimization = true;
    p.max_routes = 4;
    p.auto_apply = true;
    
    profiles.set(p);
    REQUIRE(profiles.has("TestProfileGame"));
    
    auto loaded = profiles.get("TestProfileGame");
    REQUIRE(loaded.game_name == "TestProfileGame");
    REQUIRE(loaded.max_routes == 4);
    
    profiles.remove("TestProfileGame");
    REQUIRE(profiles.has("TestProfileGame") == false);
    std::filesystem::remove_all(storage_root, error);
}

TEST_CASE("SpeedTest::servers") {
    SpeedTest st;
    auto servers = st.getServers();
    REQUIRE(servers.size() == 10);
    REQUIRE(servers[0].name.empty() == false);
}

TEST_CASE("SpeedTest exposes no worker-thread callback API") {
    CHECK_FALSE(HasBenchmarkCallbackType<SpeedTest>::value);
    CHECK_FALSE(HasBenchmarkCallbackSetter<SpeedTest>::value);
}

#ifndef PLATFORM_WINDOWS
TEST_CASE("SpeedTest does not start unavailable measurements") {
    SpeedTest st;
    st.runBenchmark("127.0.0.1");
    REQUIRE_FALSE(st.isRunning());
    CHECK(st.getResults().empty());
    CHECK(st.benchmarkServer("127.0.0.1").error == DiagnosticError::UnsupportedCapability);
}
#endif

TEST_CASE("DNSManager::presets") {
    DNSManager dm;
    auto presets = dm.getPresets();
    REQUIRE(presets.size() == 7);
    bool hasCF = false;
    for (const auto& p : presets) {
        if (p.name == "Cloudflare") hasCF = true;
    }
    REQUIRE(hasCF);
}

TEST_CASE("DNSManager rejects malformed addresses before measurement") {
    DNSManager manager;
    const auto result = manager.benchmarkServer("not-an-ip-address");

    CHECK_FALSE(result.success);
    CHECK(measurementError(result) == DiagnosticError::MalformedResponse);
}

TEST_CASE("DNSManager preserves cancellation after a successful probe") {
    CancellationSource source;
    int probe_calls = 0;
    DNSManager manager([&](const Ipv4Address&, uint32_t) {
        ++probe_calls;
        source.cancel();
        return true;
    });

    const auto result = manager.benchmarkServer("1.1.1.1", source.token());

    CHECK(probe_calls == 1);
    CHECK_FALSE(result.success);
    CHECK(result.error == DiagnosticError::Cancelled);
}

TEST_CASE("diagnostic measurements explicitly report their platform support") {
#ifdef PLATFORM_WINDOWS
    CHECK(isMeasurementSupported<PingMonitor>());
    CHECK(isMeasurementSupported<DNSManager>());
    CHECK(isMeasurementSupported<SpeedTest>());
#else
    CHECK_FALSE(isMeasurementSupported<PingMonitor>());
    CHECK_FALSE(isMeasurementSupported<DNSManager>());
    CHECK_FALSE(isMeasurementSupported<SpeedTest>());

    DNSManager manager;
    CHECK(measurementError(manager.benchmarkServer("1.1.1.1")) ==
          DiagnosticError::UnsupportedCapability);
#endif
}

#ifndef PLATFORM_WINDOWS
TEST_CASE("PingMonitor refuses to start an unsupported native worker") {
    PingMonitor monitor;
    monitor.start("1.1.1.1");
    CHECK_FALSE(monitor.isRunning());
}
#endif

TEST_CASE("ProcessMonitor::getTopProcesses") {
    ProcessMonitor pm;
    auto procs = pm.getTopProcesses(10);
#ifdef PLATFORM_WINDOWS
    REQUIRE(procs.size() <= 10);
    REQUIRE(procs.size() > 0);
#else
    REQUIRE(procs.empty());
#endif
}

TEST_CASE("ProcessMonitor rejects negative and caps oversized result counts") {
    std::vector<ProcessInfo> processes(200);
    ProcessMonitor monitor([processes] { return processes; });

    CHECK(monitor.getTopProcesses(-1).empty());
    CHECK(monitor.getTopProcesses(10000).size() == 100);
}

TEST_CASE("JitterCalculator::basic") {
    JitterCalculator jc;
    jc.addSample(10.0);
    jc.addSample(12.0);
    jc.addSample(11.0);
    
    auto stats = jc.getStats();
    REQUIRE(stats.sample_count == 2);
    REQUIRE(stats.average_jitter_ms == 1.5);
}

TEST_CASE("PacketLossMonitor measures through an injected probe") {
    int probe_calls = 0;
    PacketLossMonitor monitor([&](const Ipv4Address&, uint32_t) {
        return ++probe_calls != 2;
    });

    const auto result = monitor.measure("8.8.8.8", 3, 1);

    CHECK(result.packets_sent == 3);
    CHECK(result.packets_received == 2);
    CHECK(result.packets_lost == 1);
    CHECK(result.error == DiagnosticError::None);
}

TEST_CASE("StatsCollector::session") {
    StatsCollector sc;
    sc.start("TestSession");
    REQUIRE(sc.isRecording() == true);
    
    NetworkSnapshot snap;
    snap.ping_ms = 30.0;
    snap.jitter_ms = 2.0;
    snap.packet_loss_percent = 0.5;
    sc.recordSnapshot(snap);
    
    sc.stop();
    REQUIRE(sc.isRecording() == false);
    
    auto session = sc.getCurrentSession();
    REQUIRE(session.total_samples == 1);
    REQUIRE(session.total_ping_ms == 30.0);
}

TEST_CASE("StatsCollector callbacks can inspect the completed session") {
    StatsCollector collector;
    bool callback_called = false;
    collector.start("callback session");
    collector.setSessionCallback([&](const SessionStats& completed) {
        callback_called = true;
        CHECK(collector.getCurrentSession().total_samples == completed.total_samples);
    });

    collector.stop();

    CHECK(callback_called);
}

TEST_CASE("GameWatcher stop interrupts a confirmed long wait") {
    GameWatcher watcher([](const CancellationToken&) {
        return std::vector<GameWatcher::ObservedGame>{};
    });
    GameWatcherConfig config;
    config.check_interval_ms = 60000;
    watcher.start(config);
    REQUIRE(waitUntil([&] { return watcher.isWaiting(); }, std::chrono::milliseconds(250)));

    const auto started = std::chrono::steady_clock::now();
    watcher.stop();
    const auto elapsed = std::chrono::steady_clock::now() - started;

    CHECK(elapsed < std::chrono::milliseconds(250));
}

TEST_CASE("GameWatcher reaps a worker stopped by its callback before restart") {
    std::atomic<int> callback_count{0};
    GameWatcher watcher([](const CancellationToken&) {
        return std::vector<GameWatcher::ObservedGame>{{"Test game", "test.exe", 42}};
    });
    watcher.setGameStartCallback([&](const std::string&, const std::string&, uint32_t) {
        ++callback_count;
        watcher.stop();
    });

    watcher.start();
    REQUIRE(waitUntil([&] { return callback_count == 1 && !watcher.isRunning(); },
                      std::chrono::milliseconds(250)));
    watcher.start();
    CHECK(waitUntil([&] { return callback_count == 2 && !watcher.isRunning(); },
                    std::chrono::milliseconds(250)));
}

TEST_CASE("PingMonitor reaps a worker stopped by its callback before restart") {
    std::atomic<int> callback_count{0};
    PingMonitor monitor([](const Ipv4Address&, uint32_t) { return true; });
    monitor.setPingCallback([&](const ICMPResult&) {
        ++callback_count;
        monitor.stop();
    });

    monitor.start("1.1.1.1", 60000);
    REQUIRE(waitUntil([&] { return callback_count == 1 && !monitor.isRunning(); },
                      std::chrono::milliseconds(250)));
    monitor.start("1.1.1.1", 60000);
    CHECK(waitUntil([&] { return callback_count == 2 && !monitor.isRunning(); },
                    std::chrono::milliseconds(250)));
}

TEST_CASE("GameWatcher external and callback stops do not deadlock") {
    std::atomic<bool> callback_entered{false};
    std::atomic<bool> allow_callback_stop{false};
    std::atomic<bool> external_stop_finished{false};
    GameWatcher watcher([](const CancellationToken&) {
        return std::vector<GameWatcher::ObservedGame>{{"Test game", "test.exe", 42}};
    });
    watcher.setGameStartCallback([&](const std::string&, const std::string&, uint32_t) {
        callback_entered = true;
        while (!allow_callback_stop) {
            std::this_thread::yield();
        }
        watcher.stop();
    });

    watcher.start();
    REQUIRE(waitUntil([&] { return callback_entered.load(); }, std::chrono::milliseconds(250)));
    std::thread external_stop([&] {
        watcher.stop();
        external_stop_finished = true;
    });
    REQUIRE(waitUntil([&] { return !watcher.isRunning(); }, std::chrono::milliseconds(250)));
    allow_callback_stop = true;
    CHECK(waitUntil([&] { return external_stop_finished.load(); }, std::chrono::milliseconds(250)));
    external_stop.join();
}

TEST_CASE("PingMonitor external and callback stops do not deadlock") {
    std::atomic<bool> callback_entered{false};
    std::atomic<bool> allow_callback_stop{false};
    std::atomic<bool> external_stop_finished{false};
    PingMonitor monitor([](const Ipv4Address&, uint32_t) { return true; });
    monitor.setPingCallback([&](const ICMPResult&) {
        callback_entered = true;
        while (!allow_callback_stop) {
            std::this_thread::yield();
        }
        monitor.stop();
    });

    monitor.start("1.1.1.1", 60000);
    REQUIRE(waitUntil([&] { return callback_entered.load(); }, std::chrono::milliseconds(250)));
    std::thread external_stop([&] {
        monitor.stop();
        external_stop_finished = true;
    });
    REQUIRE(waitUntil([&] { return !monitor.isRunning(); }, std::chrono::milliseconds(250)));
    allow_callback_stop = true;
    CHECK(waitUntil([&] { return external_stop_finished.load(); }, std::chrono::milliseconds(250)));
    external_stop.join();
}

TEST_CASE("GameWatcher stop cancels an in-progress scan provider") {
    std::atomic<bool> provider_entered{false};
    GameWatcher watcher([&](const CancellationToken& cancellation) {
        provider_entered = true;
        while (!cancellation.isCancelled()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return std::vector<GameWatcher::ObservedGame>{};
    });

    watcher.start();
    REQUIRE(waitUntil([&] { return provider_entered.load(); }, std::chrono::milliseconds(250)));
    const auto started = std::chrono::steady_clock::now();
    watcher.stop();
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(250));
}

TEST_CASE("GameWatcher checkNow works while the watcher is idle") {
    GameWatcher watcher([](const CancellationToken&) {
        return std::vector<GameWatcher::ObservedGame>{{"Manual game", "manual.exe", 7}};
    });

    const auto games = watcher.checkNow();

    REQUIRE(games.size() == 1);
    CHECK(games.front() == std::make_pair(std::string("Manual game"), uint32_t{7}));
}
