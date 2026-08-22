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
        if (p.title.find("VCRUNTIME140") != std::string::npos) hasTitle = true;
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
        if (c.category == "\xD0\xA1\xD0\xB5\xD1\x82\xD1\x8C") hasNetwork = true;
        if (c.category == "FPS") hasFps = true;
        if (c.category == "\xD0\xA1\xD0\xB8\xD1\x81\xD1\x82\xD0\xB5\xD0\xBC\xD0\xB0") hasSystem = true;
    }
    REQUIRE(hasNetwork);
    REQUIRE(hasFps);
    REQUIRE(hasSystem);
}


// ===================== v1.5: safe remediation, stats, capability matrix =====================

#include "remediation/remediation_service.h"
#include "core/network_stats.h"
#include "core/capability_matrix.h"
#include "core/plain_language.h"
#include "remediation/legacy_bridge.h"
#include "core/server_map_model.h"

#include <filesystem>
#include <map>

namespace {

using namespace gno::remediation;

class FakeStateApi : public WindowsStateApi {
public:
    std::map<std::string, DnsValue> dns;
    std::map<std::string, MtuValue> mtu;
    std::map<int, RegistryData> registry;
    PowerPlanValue plan{"381b4222-f694-41f0-9685-ff5bb260df2e"};
    std::map<std::string, FullscreenValue> fullscreen;
    std::map<std::uint32_t, PriorityLevel> priority;
    bool fail_writes = false;

    Result<DnsValue> getDns(const InterfaceTarget& i) override { return dns[i.id]; }
    SimpleResult setDns(const InterfaceTarget& i, const DnsValue& v) override {
        if (fail_writes) return Fail(RemediationError::ApplyFailed, "injected");
        dns[i.id] = v; return Ok();
    }
    Result<MtuValue> getMtu(const InterfaceTarget& i) override { return mtu[i.id]; }
    SimpleResult setMtu(const InterfaceTarget& i, const MtuValue& v) override {
        if (fail_writes) return Fail(RemediationError::ApplyFailed, "injected");
        mtu[i.id] = v; return Ok();
    }
    Result<RegistryData> getAllowedRegistry(AllowedRegistryKey k) override { return registry[static_cast<int>(k)]; }
    SimpleResult setAllowedRegistry(AllowedRegistryKey k, const RegistryData& v) override {
        if (fail_writes) return Fail(RemediationError::ApplyFailed, "injected");
        registry[static_cast<int>(k)] = v; return Ok();
    }
    Result<PowerPlanValue> getPowerPlan() override { return plan; }
    SimpleResult setPowerPlan(const PowerPlanValue& v) override {
        if (fail_writes) return Fail(RemediationError::ApplyFailed, "injected");
        plan = v; return Ok();
    }
    Result<FullscreenValue> getFullscreenOptimizations(const ExecutableTarget& e) override { return fullscreen[e.path]; }
    SimpleResult setFullscreenOptimizations(const ExecutableTarget& e, const FullscreenValue& v) override {
        if (fail_writes) return Fail(RemediationError::ApplyFailed, "injected");
        fullscreen[e.path] = v; return Ok();
    }
    Result<PriorityLevel> getPriority(const ProcessTarget& p) override { return priority[p.pid]; }
    SimpleResult setPriority(const ProcessTarget& p, PriorityLevel l) override {
        if (fail_writes) return Fail(RemediationError::ApplyFailed, "injected");
        priority[p.pid] = l; return Ok();
    }
};

class MemoryBackupStore : public IBackupStore {
public:
    std::map<std::string, TransactionRecord> records;
    SimpleResult save(const TransactionRecord& r) override { records[r.transaction_id] = r; return Ok(); }
    Result<TransactionRecord> load(const std::string& id) override {
        auto it = records.find(id);
        if (it == records.end()) return Fail(RemediationError::InternalFailure, "not found");
        return it->second;
    }
    Result<std::vector<TransactionSummary>> list() override {
        std::vector<TransactionSummary> out;
        for (auto& [id, rec] : records) out.push_back({id, rec.status});
        return out;
    }
};

std::vector<std::optional<ActionTarget>> testTargets(const std::vector<FixAction*>& actions) {
    std::vector<std::optional<ActionTarget>> targets;
    for (auto* a : actions) {
        switch (a->id()) {
            case ActionId::FullscreenOptimizations:
                targets.push_back(ActionTarget{ExecutableTarget{"C:\\Games\\game.exe"}}); break;
            case ActionId::Dns:
            case ActionId::Mtu:
                targets.push_back(ActionTarget{InterfaceTarget{"{8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c}", 1}}); break;
            case ActionId::ProcessPriority:
                targets.push_back(ActionTarget{ProcessTarget{1234, 5678, "C:\\Games\\game.exe"}}); break;
            default:
                targets.push_back(ActionTarget{NoTarget{}});
        }
    }
    return targets;
}

} // namespace

TEST_CASE("Remediation observe apply rollback roundtrip") {
    FakeStateApi api;
    MemoryBackupStore store;
    RemediationService service(api, store, testTargets);

    auto before = service.observeAll();
    { std::ostringstream dbg; dbg << (int)before.code() << ':' << before.detail(); REQUIRE_MESSAGE(before.ok(), dbg.str()); }
    CHECK(before.value().size() == 7);

    auto applied = service.applyAll();
    REQUIRE(applied.ok());
    REQUIRE(applied.value().succeeded);
    CHECK(applied.value().status == TransactionStatus::Applied);
    for (const auto& o : applied.value().outcomes)
        CHECK(o.status == ActionStatus::Applied);

    CHECK(api.plan.identifier == "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c");
    CHECK(api.dns["{8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c}"].automatic == false);
    CHECK(api.mtu["{8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c}"].bytes == 1500);

    auto history = service.history();
    REQUIRE(history.ok());
    REQUIRE(history.value().size() == 1);

    auto rolled = service.rollback(history.value().front().transaction_id);
    REQUIRE(rolled.ok());
    REQUIRE(rolled.value().succeeded);

    auto after = service.observeAll();
    REQUIRE(after.ok());
    REQUIRE(after.value().size() == before.value().size());
    for (std::size_t i = 0; i < before.value().size(); ++i)
        CHECK(before.value()[i].current_state == after.value()[i].current_state);
}

TEST_CASE("Remediation failed write marks transaction failed") {
    FakeStateApi api;
    api.fail_writes = true;
    MemoryBackupStore store;
    RemediationService service(api, store, testTargets);

    auto applied = service.applyAll();
    { std::ostringstream dbg; dbg << (int)applied.code() << ':' << (applied.ok() ? applied.value().detail : std::string()); REQUIRE_MESSAGE(applied.ok(), dbg.str()); }
    CHECK_FALSE(applied.value().succeeded);
    CHECK(applied.value().status == TransactionStatus::Failed);
}

TEST_CASE("JsonBackupStore save load list roundtrip") {
    FakeStateApi api;
    auto dir = std::filesystem::temp_directory_path() / "gno_backup_test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    JsonBackupStore store(dir.string());

    MemoryBackupStore seed;
    RemediationService svc(api, seed, testTargets);
    (void)svc.applyAll();
    REQUIRE(seed.records.size() == 1);
    const TransactionRecord& original = seed.records.begin()->second;

    REQUIRE(store.save(original).ok());

    auto loaded = store.load(original.transaction_id);
    REQUIRE(loaded.ok());
    CHECK(loaded.value().transaction_id == original.transaction_id);
    const bool records_equal = (loaded.value() == original);
    CHECK(records_equal);

    auto list = store.list();
    REQUIRE(list.ok());
    CHECK(list.value().size() == 1);

    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("NetworkStatistics computes descriptive metrics") {
    auto s = NetworkStatistics::compute({10, 20, 30, 40, 50});
    CHECK(s.mean == doctest::Approx(30.0));
    CHECK(s.median == doctest::Approx(30.0));
    CHECK(s.p95 == doctest::Approx(50.0));
    CHECK(s.min == doctest::Approx(10.0));
    CHECK(s.max == doctest::Approx(50.0));
    const double sd = s.stddev;
    CHECK(sd > 14.0);
    CHECK(sd < 16.0);
    CHECK(NetworkStatistics::packetLossPercent(10, 8) == doctest::Approx(20.0));
    CHECK(NetworkStatistics::packetLossPercent(0, 0) == doctest::Approx(0.0));
}

TEST_CASE("CapabilityMatrix and PlainLanguageReport produce sections") {
    bool allowlist_only_in_full = CapabilityMatrix::canDo(true).size() >= CapabilityMatrix::canDo(false).size();
    CHECK(allowlist_only_in_full);
    CHECK_FALSE(CapabilityMatrix::cannotDo().empty());

    auto sections = PlainLanguageReport::build(150.0, 3.0, 20.0, true, nullptr, false);
    REQUIRE(sections.size() == 4);
    CHECK_FALSE(sections[0].lines.empty());
    CHECK_FALSE(sections[2].lines.empty());
}
TEST_CASE("Legacy bridge routes mapped fixes through transactions") {
    FakeStateApi api;
    MemoryBackupStore store;

    auto result = gno::remediation::applySafeFix("power_plan", api, store);
    CHECK_FALSE(result.empty());
    CHECK(api.plan.identifier == "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c");
    CHECK(store.records.size() == 1);

    CHECK(gno::remediation::applySafeFix("totally_unknown", api, store).empty());
    CHECK(store.records.size() == 1);
}
TEST_CASE("applyIds applies only requested subset") {
    FakeStateApi api;
    MemoryBackupStore store;
    RemediationService service(api, store, testTargets);

    auto r = service.applyIds({ActionId::PowerPlan});
    REQUIRE(r.ok());
    CHECK(r.value().succeeded);
    CHECK(api.plan.identifier == "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c");
    CHECK(api.dns.empty());
    CHECK(api.mtu.empty());
    CHECK(r.value().outcomes.size() == 1);
}
TEST_CASE("ServerMapModel grade, region filter, best server") {
    using namespace gno;
    CHECK(ServerMapModel::grade(-1) == MapGrade::Unknown);
    CHECK(ServerMapModel::grade(20) == MapGrade::Good);
    CHECK(ServerMapModel::grade(80) == MapGrade::Medium);
    CHECK(ServerMapModel::grade(250) == MapGrade::Bad);

    std::vector<MapServer> servers = {
        {"a", "a", "Germany", "1.1.1.1", 50, 8, 20},
        {"b", "b", "USA", "2.2.2.2", 40, -74, 60},
        {"c", "c", "Japan", "3.3.3.3", 35, 139, 90},
        {"d", "d", "Brazil", "4.4.4.4", -23, -46, 15},
    };
    auto eu = ServerMapModel::filterByRegion(servers, "EU");
    CHECK(eu.size() == 1);
    CHECK(ServerMapModel::filterByRegion(servers, "all").size() == 4);
    CHECK(ServerMapModel::bestServer(servers) == 3);
    CHECK(ServerMapModel::regionOf("Japan") == "ASIA");
}