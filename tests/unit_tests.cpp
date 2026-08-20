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
#include <filesystem>
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

TEST_CASE("JitterCalculator::basic") {
    JitterCalculator jc;
    jc.addSample(10.0);
    jc.addSample(12.0);
    jc.addSample(11.0);
    
    auto stats = jc.getStats();
    REQUIRE(stats.sample_count == 2);
    REQUIRE(stats.average_jitter_ms == 1.5);
}

TEST_CASE("PacketLossMonitor::measure") {
    PacketLossMonitor plm;
    auto result = plm.measure("8.8.8.8", 10, 1000);
#ifdef PLATFORM_WINDOWS
    REQUIRE(result.packets_sent == 10);
    REQUIRE(result.loss_percent >= 0.0);
#else
    REQUIRE(result.packets_sent == 0);
#endif
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

TEST_CASE("GameWatcher stop interrupts a long configured wait") {
    GameWatcher watcher;
    GameWatcherConfig config;
    config.check_interval_ms = 60000;
    watcher.start(config);

    const auto started = std::chrono::steady_clock::now();
    watcher.stop();
    const auto elapsed = std::chrono::steady_clock::now() - started;

    CHECK(elapsed < std::chrono::milliseconds(250));
}
