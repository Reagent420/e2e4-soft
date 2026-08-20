#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "core/game_detector.h"
#include "core/route_analyzer.h"
#include "core/game_profiles.h"
#include "core/speed_test.h"
#include "core/dns_manager.h"
#include "core/process_monitor.h"
#include "core/system_audit.h"
#include "core/launch_diagnostics.h"
#include "core/problem_db.h"
#include "monitoring/jitter_calculator.h"
#include "monitoring/packet_loss_monitor.h"
#include "monitoring/stats_collector.h"

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
    REQUIRE(routes.size() > 0);
    bool hasValid = false;
    for (const auto& r : routes) {
        if (!r.interface_name.empty() || r.gateway_ip != "0.0.0.0") {
            hasValid = true;
            break;
        }
    }
    REQUIRE(hasValid);
}

TEST_CASE("GameProfiles::save/load") {
    GameProfiles profiles;
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
}

TEST_CASE("SpeedTest::servers") {
    SpeedTest st;
    auto servers = st.getServers();
    REQUIRE(servers.size() == 10);
    REQUIRE(servers[0].name.empty() == false);
}

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

TEST_CASE("ProcessMonitor::getTopProcesses") {
    ProcessMonitor pm;
    auto procs = pm.getTopProcesses(10);
    REQUIRE(procs.size() <= 10);
    REQUIRE(procs.size() > 0);
}

TEST_CASE("ProcessMonitor::block/unblock") {
    ProcessMonitor pm;
    pm.blockProcess("testblock.exe");
    auto blocked = pm.getBlockedProcesses();
    REQUIRE(std::find(blocked.begin(), blocked.end(), "testblock.exe") != blocked.end());
    
    pm.unblockProcess("testblock.exe");
    blocked = pm.getBlockedProcesses();
    REQUIRE(std::find(blocked.begin(), blocked.end(), "testblock.exe") == blocked.end());
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
    REQUIRE(result.packets_sent == 10);
    REQUIRE(result.loss_percent >= 0.0);
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

TEST_CASE("ProblemDb::known games and fixes") {
    auto games = gno::ProblemDb::getKnownGames();
    REQUIRE(games.size() >= 5);
    REQUIRE(games[0] == "CS2");

auto problems = gno::ProblemDb::getForGame("CS2");
    REQUIRE(problems.size() >= 2);
    bool hasTitle = false;
    for (const auto& p : problems)
        if (p.title.find("пинг") != std::string::npos) hasTitle = true;
    REQUIRE(hasTitle);
}

TEST_CASE("ProblemDb::getForGame is case-insensitive") {
    auto a = gno::ProblemDb::getForGame("valorant");
    auto b = gno::ProblemDb::getForGame("Valorant");
    REQUIRE(a.size() == b.size());
    REQUIRE(a.size() >= 2);
}

TEST_CASE("ProblemDb::auto-fix ids map to known actions") {
    const std::vector<std::string> known = {"power_plan", "game_dvr", "fullscreen_opt",
                                            "dns", "mtu", "tcp", "vm", "priority",
                                            "tcp_mtu", "dns_tcp", "fps_boost"};
    for (const auto& p : gno::ProblemDb::getAll()) {
        if (p.fix_action.empty()) continue;
        bool knownId = false;
        for (const auto& k : known)
            if (k == p.fix_action) knownId = true;
        REQUIRE(knownId);
    }
}

TEST_CASE("SystemAudit::formatChanges renders old->new") {
    gno::SettingChange c;
    c.section = "Network";
    c.name = "TCP NoDelay";
    c.action = "enable";
    c.old_value = "";
    c.new_value = "1";
    c.status = gno::SettingChange::Status::Applied;
    c.detail = "ok";
    std::vector<gno::SettingChange> list{c};
    std::string out = gno::SystemAudit::formatChanges(list);
    REQUIRE(out.find("[APPLIED]") != std::string::npos);
    REQUIRE(out.find("TCP NoDelay") != std::string::npos);
    REQUIRE(out.find("old") != std::string::npos);
}

TEST_CASE("SystemAudit::capabilities have sensible statuses") {
    auto caps = gno::SystemAudit::getCapabilities();
    REQUIRE(caps.size() >= 10);
    bool hasVpn = false, hasLocal = false;
    for (const auto& c : caps) {
        if (c.requires_vpn_server) hasVpn = true;
        else hasLocal = true;
    }
    REQUIRE(hasVpn);
    REQUIRE(hasLocal);
}

TEST_CASE("LaunchDiagnostics::checks cover categories") {
    gno::GameDiagnostics d = gno::LaunchDiagnostics::run("TestGame", "test_game.exe");
    REQUIRE(d.checks.size() >= 8);
    bool hasNetwork = false, hasFps = false, hasSystem = false;
    for (const auto& c : d.checks) {
        if (c.category == "Сеть") hasNetwork = true;
        if (c.category == "FPS") hasFps = true;
        if (c.category == "Система") hasSystem = true;
    }
    REQUIRE(hasNetwork);
    REQUIRE(hasFps);
    REQUIRE(hasSystem);
}
