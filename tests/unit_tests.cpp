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
#include "core/connection_grader.h"
#include "core/problem_db.h"
#include "monitoring/jitter_calculator.h"
#include "monitoring/packet_loss_monitor.h"
#include "monitoring/stats_collector.h"
#include "core/session_history.h"
#include "core/logger.h"
#include "core/report_text.h"
#include "core/sha256.h"
#include "core/simple_json.h"
#include "core/updater.h"
#include "core/multipath_engine.h"
#include "diagnostics/diagnostic_types.h"
#include "diagnostics/endpoint_observer.h"
#include "diagnostics/network_sampler.h"
#include "diagnostics/probe_client.h"
#include "diagnostics/diagnostics_service.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <map>
#include <string>
#include <vector>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

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

TEST_CASE("GameProfiles::importFromFile keeps saved profiles on bad/empty import") {
    const auto tmp = std::filesystem::temp_directory_path() / "gno_profiles_import.json";
    std::error_code ec;
    std::filesystem::remove(tmp, ec);

    GameProfiles profiles;
    profiles.remove("ImportTestGame");
    profiles.remove("ImportedGame");

    GameProfile keep;
    keep.game_name = "ImportTestGame";
    keep.process_name = "importtest.exe";
    profiles.set(keep);
    REQUIRE(profiles.has("ImportTestGame"));

    // Мусорный файл не должен затирать сохранённые профили: раньше profiles_
    // очищался до разбора и тут же сохранялся пустым.
    {
        std::ofstream bad(tmp);
        bad << "not a json at all";
    }
    CHECK(profiles.importFromFile(tmp.string()) == false);
    CHECK(profiles.has("ImportTestGame"));

    // Пустой список profiles — тоже неудачный импорт, а не удаление настроек.
    {
        std::ofstream empty(tmp);
        empty << "{\"version\": 1, \"profiles\": []}";
    }
    CHECK(profiles.importFromFile(tmp.string()) == false);
    CHECK(profiles.has("ImportTestGame"));

    // Корректный файл импортируется (с заменой всего набора).
    {
        std::ofstream good(tmp);
        good << "{\"version\": 1, \"profiles\": [{"
             << "  \"game_name\": \"ImportedGame\","
             << "  \"process_name\": \"imported.exe\","
             << "  \"game_dvr_opt\": true"
             << "}]}";
    }
    CHECK(profiles.importFromFile(tmp.string()));
    CHECK(profiles.has("ImportedGame"));
    CHECK(profiles.get("ImportedGame").game_dvr_opt == true);

    // Многострочный формат, который реально выдаёт exportToFile, должен
    // импортироваться обратно (round-trip). Раньше обрезка скобок по краям
    // съедала закрывающую скобку последнего объекта.
    GameProfile roundtrip;
    roundtrip.game_name = "RoundtripGame";
    roundtrip.process_name = "roundtrip.exe";
    roundtrip.mtu_opt = true;
    roundtrip.tcp_opt = false;
    roundtrip.auto_apply = true;
    profiles.set(roundtrip);
    REQUIRE(profiles.has("RoundtripGame"));
    REQUIRE(profiles.exportToFile(tmp.string()));

    // Удаляем оба профиля из хранилища и возвращаем их импортом.
    profiles.remove("ImportedGame");
    profiles.remove("RoundtripGame");
    REQUIRE(profiles.has("RoundtripGame") == false);

    REQUIRE(profiles.importFromFile(tmp.string()));
    CHECK(profiles.has("RoundtripGame"));
    CHECK(profiles.get("RoundtripGame").mtu_opt == true);
    CHECK(profiles.get("RoundtripGame").tcp_opt == false);
    CHECK(profiles.get("RoundtripGame").auto_apply == true);

    profiles.remove("RoundtripGame");
    std::filesystem::remove(tmp, ec);
}

TEST_CASE("GameProfiles: экземпляр «только чтение» не затирает чужие изменения") {
    // GUI (GameProfilesWidget) и воркер MonitoringService создают каждый свой
    // экземпляр, но работают с одним файлом. Раньше деструктор сохранял своё
    // состояние безусловно: воркер, прочитавший старый снимок, при выходе
    // затирал профиль, сохранённый GUI в параллельном потоке.
    GameProfiles writer;
    writer.remove("RaceGame");

    GameProfile p;
    p.game_name = "RaceGame";
    p.process_name = "race.exe";
    p.max_routes = 5;
    writer.set(p);
    REQUIRE(writer.has("RaceGame"));

    // Этот экземпляр только читает — как делают handleGameStart и
    // discoverRunningGameProcess.
    {
        GameProfiles reader;
        REQUIRE(reader.has("RaceGame"));
        REQUIRE(reader.get("RaceGame").max_routes == 5);
    }

    // После разрушения «читателя» сохранённые данные должны быть на месте.
    // (Если читатель сохранил свой снимок — данные те же, но если GUI успел
    // изменить их между чтением и выходом читателя — они бы потерялись.)
    GameProfiles checker;
    CHECK(checker.has("RaceGame"));
    CHECK(checker.get("RaceGame").max_routes == 5);

    checker.remove("RaceGame");
    REQUIRE_FALSE(checker.has("RaceGame"));
}

TEST_CASE("GameProfiles: параллельные читатели не портят файл") {
    // Файловая гонка: несколько потоков создают свои экземпляры и читают
    // profiles.json одновременно. До мьютекса чтение могло попасть на
    // частично записанный файл. Здесь важно, что ничего не падает и данные
    // согласованы — независимо от того, записывает ли кто-то в этот момент.
    GameProfiles profiles;
    profiles.remove("ConcurrentGame");

    GameProfile p;
    p.game_name = "ConcurrentGame";
    p.process_name = "concurrent.exe";
    profiles.set(p);

    std::atomic<int> ok_reads{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t) {
        threads.emplace_back([&ok_reads]() {
            GameProfiles reader;
            if (reader.has("ConcurrentGame")) ok_reads.fetch_add(1);
        });
    }
    for (auto& th : threads) th.join();

    CHECK(ok_reads.load() == 8);

    profiles.remove("ConcurrentGame");
    REQUIRE_FALSE(profiles.has("ConcurrentGame"));
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

TEST_CASE("ConnectionGrader: нулевые входы — это «нет данных», а не A+") {
    // Раньше evaluate(0,0,0) набирало 100 баллов и рисовало «A+ Excellent» —
    // хотя на самом деле это ни одного замера: сессия без семплов (handleGameEnd
    // при session_samples_ == 0) или свежий сервис до первого пинга.
    const auto q = ConnectionGrader::evaluate(0.0, 0.0, 0.0);
    CHECK(q.grade == QualityGrade::Unknown);
    CHECK(q.score == 0);
    CHECK(q.grade_str == "--");
    CHECK(q.ping_score == 0);
    CHECK(q.jitter_score == 0);
    CHECK(q.loss_score == 0);
}

TEST_CASE("ConnectionGrader: отрицательные входы тоже считаются отсутствием данных") {
    const auto q = ConnectionGrader::evaluate(-1.0, -1.0, -1.0);
    CHECK(q.grade == QualityGrade::Unknown);
    CHECK(q.score == 0);
}

TEST_CASE("ConnectionGrader: реальные метрики сортируются по буквам") {
    // Веса: ping 40%, jitter 30%, loss 30%.
    // (15,2,0)   -> 100/100/100 -> 100 -> A+
    // (45,8,0.3) ->  80/ 90/ 90 ->  86 -> A
    // (90,25,2.5)->  60/ 60/ 40 ->  54 -> D
    // (300,150,20)->  0/  0/  0 ->   0 -> F
    CHECK(ConnectionGrader::evaluate(15.0, 2.0, 0.0).grade == QualityGrade::A_Plus);
    CHECK(ConnectionGrader::evaluate(45.0, 8.0, 0.3).grade == QualityGrade::A);
    CHECK(ConnectionGrader::evaluate(90.0, 25.0, 2.5).grade == QualityGrade::D);
    CHECK(ConnectionGrader::evaluate(300.0, 150.0, 20.0).grade == QualityGrade::F);
    // Любой положительный пинг — это уже данные, даже при идеальных джиттере/потерях.
    CHECK(ConnectionGrader::evaluate(1.0, 0.0, 0.0).grade != QualityGrade::Unknown);
}

TEST_CASE("ConnectionGrader: Unknown корректно отображается во всех текстовых представлениях") {
    CHECK(ConnectionGrader::gradeToString(QualityGrade::Unknown) == "--");
    CHECK_FALSE(ConnectionGrader::gradeDescription(QualityGrade::Unknown).empty());
    CHECK_FALSE(ConnectionGrader::gradeColor(QualityGrade::Unknown).empty());
}

TEST_CASE("LaunchDiagnostics::applyDnsDetailed отвалидировывает адреса до применения") {
    // Адреса подставляются в netsh-команду, поэтому мусор не должен доходить
    // до системы вообще. Раньше кнопка «Применить» ничего не проверяла
    // (потому что ничего и не применяла).
    CHECK_FALSE(LaunchDiagnostics::applyDnsDetailed({"not-an-ip"}).applied);
    CHECK_FALSE(LaunchDiagnostics::applyDnsDetailed({"8.8.8.8", "garbage"}).applied);
    CHECK_FALSE(LaunchDiagnostics::applyDnsDetailed({"999.1.1.1"}).applied);
    CHECK_FALSE(LaunchDiagnostics::applyDnsDetailed({}).applied); // нет серверов
    // Multicast/broadcast не имеют смысла как DNS.
    CHECK_FALSE(LaunchDiagnostics::applyDnsDetailed({"224.0.0.1"}).applied);

    // Корректный адрес проходит валидацию; применение может не удаться
    // (нет адаптера/нет прав), но это уже не ошибка валидации.
    const auto ok = LaunchDiagnostics::applyDnsDetailed({"8.8.8.8", "8.8.4.4"});
    CHECK_FALSE(ok.message.empty());
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
                                            "tcp_mtu", "dns_tcp", "fps_boost", "cs2_maxping"};
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
#include "remediation/windows_fix_action.h"
#include "core/network_stats.h"
#include "core/capability_matrix.h"
#include "core/plain_language.h"
#include "remediation/legacy_bridge.h"
#include "core/server_map_model.h"
#include "core/autopilot_plan.h"
#include "core/sqlite_history_store.h"
#include <filesystem>
#include <map>
#include <sstream>

TEST_CASE("SqliteHistoryStore: insert, query, aggregate") {
    auto db_path = (std::filesystem::temp_directory_path() / "gno_test.db").string();
    std::error_code rm_ec;
    std::filesystem::remove(db_path, rm_ec);

    gno::SqliteHistoryStore store(db_path);
    CHECK(store.totalSessions() == 0);

    gno::HistoryRecord r1;
    r1.game_name = "CS2";
    r1.avg_ping_ms = 23.5;
    r1.quality_score = 85;
    store.insert(r1);

    gno::HistoryRecord r2;
    r2.game_name = "PUBG";
    r2.avg_ping_ms = 45.0;
    r2.quality_score = 60;
    store.insert(r2);

    CHECK(store.totalSessions() == 2);
    CHECK(store.averageScore() == doctest::Approx(72.5));

    auto last = store.getLast(1);
    REQUIRE(last.size() == 1);
    CHECK(last[0].game_name == "PUBG");

    std::filesystem::remove(db_path, rm_ec);
}
#include "core/net_utils.h"
#include "core/report_exporter.h"
#include "core/profile_engine.h"
#include "core/alert_thresholds.h"

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
    // Какое именно действие должно упасть: нужно, чтобы проверить, что одно
    // не применившееся действие не отменяет остальные.
    std::set<ActionId> fail_for;
    bool shouldFail(ActionId id) const { return fail_for.count(id) > 0; }

    // Хук для проверки параллельного запуска: вызывается в начале getDns.
    // Нужен, чтобы детерминированно «зависнуть» внутри первой операции, пока
    // вторая пытается начаться (см. тест про Busy).
    std::function<void()> on_get_dns;

    Result<DnsValue> getDns(const InterfaceTarget& i) override {
        if (on_get_dns) on_get_dns();
        return dns[i.id];
    }
    SimpleResult setDns(const InterfaceTarget& i, const DnsValue& v) override {
        if (fail_writes || shouldFail(ActionId::Dns)) return Fail(RemediationError::ApplyFailed, "injected");
        dns[i.id] = v; return Ok();
    }
    Result<MtuValue> getMtu(const InterfaceTarget& i) override { return mtu[i.id]; }
    SimpleResult setMtu(const InterfaceTarget& i, const MtuValue& v) override {
        if (fail_writes || shouldFail(ActionId::Mtu)) return Fail(RemediationError::ApplyFailed, "injected");
        mtu[i.id] = v; return Ok();
    }
    Result<RegistryData> getAllowedRegistry(AllowedRegistryKey k) override { return registry[static_cast<int>(k)]; }
    SimpleResult setAllowedRegistry(AllowedRegistryKey k, const RegistryData& v) override {
        if (fail_writes) return Fail(RemediationError::ApplyFailed, "injected");
        if (k == AllowedRegistryKey::TcpInitialRetransmissionTimeout && shouldFail(ActionId::TcpParameters))
            return Fail(RemediationError::ApplyFailed, "injected");
        if (k == AllowedRegistryKey::GameModeAllow && shouldFail(ActionId::GameMode))
            return Fail(RemediationError::ApplyFailed, "injected");
        registry[static_cast<int>(k)] = v; return Ok();
    }
    Result<PowerPlanValue> getPowerPlan() override { return plan; }
    SimpleResult setPowerPlan(const PowerPlanValue& v) override {
        if (fail_writes || shouldFail(ActionId::PowerPlan)) return Fail(RemediationError::ApplyFailed, "injected");
        plan = v; return Ok();
    }
    Result<FullscreenValue> getFullscreenOptimizations(const ExecutableTarget& e) override { return fullscreen[e.path]; }
    SimpleResult setFullscreenOptimizations(const ExecutableTarget& e, const FullscreenValue& v) override {
        if (fail_writes) return Fail(RemediationError::ApplyFailed, "injected");
        fullscreen[e.path] = v; return Ok();
    }
    gno::remediation::MouseAccelValue accel{1, 6, 10};
    std::map<std::uint32_t, std::uint32_t> cs2;
    // Как на машине без установленной CS2: наблюдение отдаёт Unsupported.
    bool cs2_unsupported = false;
    Result<Cs2MaxPingValue> getCs2MaxPing() override {
        if (cs2_unsupported)
            return Fail(RemediationError::Unsupported, "CS2 не найден (Steam cfg)");
        auto it = cs2.find(1);
        return Cs2MaxPingValue{it == cs2.end() ? 30u : it->second};
    }
    SimpleResult setCs2MaxPing(const Cs2MaxPingValue& v) override {
        if (fail_writes) return Fail(RemediationError::ApplyFailed, "injected");
        cs2[1] = v.max_ping; return Ok();
    }
    Result<gno::remediation::MouseAccelValue> getMouseAccel() override { return accel; }
    gno::remediation::SimpleResult setMouseAccel(const gno::remediation::MouseAccelValue& v) override {
        if (fail_writes) return Fail(RemediationError::ApplyFailed, "injected");
        accel = v; return Ok();
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
    CHECK(before.value().size() == 10);

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
    // Провал обязан быть виден в Result, а не только внутри outcome: пока ok()
    // оставался истинным, любой вызывающий, проверявший только его, считал
    // операцию удавшейся (так «успешно» снимался маркер буста после
    // провалившегося отката).
    CHECK_FALSE(applied.ok());
    CHECK(applied.code() != RemediationError::None);
    // Но сами результаты никуда не делись — UI показывает их построчно.
    CHECK_FALSE(applied.value().succeeded);
    CHECK(applied.value().status == TransactionStatus::Failed);
    CHECK_FALSE(applied.value().outcomes.empty());
}

TEST_CASE("Откат несуществующей транзакции — ошибка в Result, а не тихий успех") {
    // Регресс: Result<ApplyOutcome> возвращался как «успешный» даже когда
    // откатывать было нечего — транзакции нет. BoostGate на этом основании
    // снимал маркер активного буста, и трей показывал выключенный буст при
    // реально применённых изменениях.
    FakeStateApi api;
    MemoryBackupStore store;
    RemediationService service(api, store, testTargets);

    const auto rolled = service.rollback("tx-does-not-exist");
    CHECK_FALSE(rolled.ok());
    CHECK_FALSE(rolled.value().succeeded);
    CHECK_FALSE(rolled.detail().empty());
}

TEST_CASE("Параллельная операция отклоняется с Busy, а не идёт одновременно") {
    // Регресс: operation_mutex_ есть у каждой транзакции, но он защищает только
    // от повторного входа в ОДНУ транзакцию. «Прайм-сетап» и «Центр изменений» —
    // соседние вкладки, и запустить применение на обеих можно было одновременно.
    // Вторая операция снимала бы «до» уже ПОСЛЕ правки первой, поэтому её бэкап
    // содержал бы не исходное значение, и откат вернул бы не то, что было.
    FakeStateApi api;
    api.dns["{8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c}"] = DnsValue{false, {"8.8.8.8"}};
    MemoryBackupStore store;
    RemediationService service(api, store, testTargets);

    std::mutex m;
    std::condition_variable cv;
    bool entered = false;
    bool release = false;
    int calls = 0;
    api.on_get_dns = [&] {
        std::unique_lock<std::mutex> lk(m);
        // Блокируем ТОЛЬКО первую операцию. Если защита однажды сломается, вторая
        // операция дойдёт до observe, но не зависнет здесь навсегда — тест упадёт
        // на CHECK, а не повиснет.
        if (calls++ > 0) return;
        entered = true;
        cv.notify_all();
        cv.wait(lk, [&] { return release; });
    };

    std::atomic<int> first_code{-1};
    std::thread first([&] {
        auto r = service.applyIds({ActionId::Dns});
        first_code.store(static_cast<int>(r.code()));
    });

    {
        std::unique_lock<std::mutex> lk(m);
        cv.wait(lk, [&] { return entered; });
    }

    // Первая операция сейчас внутри observe. Вторая обязана получить отказ.
    const auto second = service.applyIds({ActionId::Dns});
    CHECK(second.code() == RemediationError::Busy);
    CHECK_FALSE(second.ok());
    CHECK_FALSE(second.detail().empty());

    // И откат — такая же операция над системой, он тоже не должен пройти.
    const auto rollback_during_apply = service.rollback("tx-anything");
    CHECK(rollback_during_apply.code() == RemediationError::Busy);

    {
        std::lock_guard<std::mutex> lk(m);
        release = true;
    }
    cv.notify_all();
    first.join();
    // Отказ второй операции не должен был сломать первую.
    CHECK(first_code.load() == static_cast<int>(RemediationError::None));

    // Блокировка снята — следующая операция проходит нормально.
    api.on_get_dns = nullptr;
    const auto after = service.rollback("tx-does-not-exist");
    CHECK(after.code() != RemediationError::Busy);
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

    // DEBUG: re-save loaded record under _re id for byte comparison
    auto dbg = store.load(list.value().front().transaction_id);
    if (dbg.ok()) {
        TransactionRecord rec2 = dbg.value();
        rec2.transaction_id += "_re";
        store.save(rec2);
    }
}

TEST_CASE("Неприменимое на этой машине действие не отменяет остальные") {
    // Регресс: prepare() работает по принципу «всё или ничего», поэтому одно
    // действие, которое на этой машине неприменимо (напр. CS2 не установлен),
    // прерывало применение целиком. Кнопка «Применить рекомендации» не делала
    // НИЧЕГО и сообщала только «CS2 не найден (Steam cfg)».
    FakeStateApi api;
    api.cs2_unsupported = true;
    MemoryBackupStore store;
    RemediationService service(api, store, testTargets);

    const auto applied = service.applyAll();
    REQUIRE(applied.ok());
    CHECK(applied.value().succeeded);
    CHECK(applied.value().status == TransactionStatus::Applied);

    // CS2 попал в «не поддерживается», а не в ошибки...
    REQUIRE(applied.value().unsupported_actions.size() == 1);
    CHECK(applied.value().unsupported_actions.front() == ActionId::Cs2MaxPing);
    // ...и не попал в применённые.
    for (const auto& o : applied.value().outcomes) CHECK(o.id != ActionId::Cs2MaxPing);

    // Остальные действия реально применились.
    CHECK(api.plan.identifier == "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c");
    CHECK(api.accel.speed == 0);
    CHECK(api.registry[static_cast<int>(AllowedRegistryKey::GameModeAllow)].value == 0);
    CHECK(api.dns["{8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c}"].automatic == false);
    CHECK(api.mtu["{8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c}"].bytes == 1500);

    // Транзакция откатывается целиком, несмотря на выброшенное действие.
    auto rolled = service.rollback(store.records.begin()->first);
    REQUIRE(rolled.ok());
    CHECK(api.accel.speed == 1);
}

TEST_CASE("Если неприменимы ВСЕ действия, транзакция не создаётся") {
    // Противоположный случай: нечего применять — значит и транзакции быть не
    // должно (иначе в журнале появлялась бы пустая запись без применённых
    // действий, которую нельзя откатить).
    FakeStateApi api;
    api.cs2_unsupported = true;
    MemoryBackupStore store;
    RemediationService service(api, store, testTargets);

    const auto applied = service.applyIds({ActionId::Cs2MaxPing});
    CHECK_FALSE(applied.ok());
    CHECK(applied.code() == RemediationError::InvalidTarget);
    CHECK(store.records.empty());
}

TEST_CASE("Журнал хранит время создания, а не время последней записи файла") {
    // Регресс: время в истории бралось из mtime файла журнала. Файл
    // перезаписывается на каждом шаге (прогресс применения, откат), поэтому
    // откаченная транзакция «переезжала» вверх списка с временем отката —
    // пользователь видел, что изменение сделано не тогда, когда оно сделано.
    auto dir = std::filesystem::temp_directory_path() / "gno_backup_created_at";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);

    JsonBackupStore store(dir.string());
    TransactionRecord older{};
    older.transaction_id = "tx-old";
    older.status = TransactionStatus::Applied;
    older.applied_action_order = {ActionId::PowerPlan};
    older.created_at = "2026-01-01 10:00:00";
    REQUIRE(store.save(older).ok());

    TransactionRecord newer{};
    newer.transaction_id = "tx-new";
    newer.status = TransactionStatus::Applied;
    newer.applied_action_order = {ActionId::GameDvr};
    newer.created_at = "2026-01-02 10:00:00";
    REQUIRE(store.save(newer).ok());

    auto list = store.list();
    REQUIRE(list.ok());
    REQUIRE(list.value().size() == 2);
    CHECK(list.value().front().transaction_id == "tx-new");
    CHECK(list.value().front().created_at == "2026-01-02 10:00:00");

    // Откатываем самую старую: файл перезаписывается (mtime становится самым
    // свежим), но время создания и место в списке меняться не должны.
    TransactionRecord reverted = older;
    reverted.status = TransactionStatus::Reverted;
    REQUIRE(store.save(reverted).ok());
    std::filesystem::last_write_time(dir / "tx-old.json",
                                     std::filesystem::file_time_type::clock::now());

    auto after = store.list();
    REQUIRE(after.ok());
    REQUIRE(after.value().size() == 2);
    CHECK(after.value().front().transaction_id == "tx-new");
    CHECK(after.value().back().transaction_id == "tx-old");
    CHECK(after.value().back().created_at == "2026-01-01 10:00:00");

    // Записи без created_at (созданные до появления поля) читаются по-прежнему:
    // для них время берётся из mtime, чтобы старый журнал не «потерял» даты.
    TransactionRecord legacy{};
    legacy.transaction_id = "tx-legacy";
    legacy.status = TransactionStatus::Reverted;
    REQUIRE(store.save(legacy).ok());
    auto legacy_list = store.list();
    REQUIRE(legacy_list.ok());
    bool legacy_found = false;
    for (const auto& item : legacy_list.value())
        if (item.transaction_id == "tx-legacy") {
            legacy_found = true;
            CHECK_FALSE(item.created_at.empty());
        }
    CHECK(legacy_found);

    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("NetworkStatistics computes descriptive metrics") {    auto s = NetworkStatistics::compute({10, 20, 30, 40, 50});
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
TEST_CASE("MapGrade distinguishes offline probes and results merge safely") {
    using namespace gno;
    CHECK(ServerMapModel::grade(-2) == MapGrade::Offline);
    CHECK(ServerMapModel::grade(-1) == MapGrade::Unknown);

    std::vector<MapServer> servers = {
        {"a", "a", "Germany", "1.1.1.1", 50, 8, -1},
        {"b", "b", "USA", "2.2.2.2", 40, -74, -1},
        {"c", "c", "Japan", "3.3.3.3", 35, 139, -1},
    };
    ServerMapModel::applyProbeResults(servers, {{0, 25}, {2, -2}});
    CHECK(servers[0].latency_ms == 25);
    CHECK(servers[1].latency_ms == -1);
    CHECK(servers[2].latency_ms == -2);
    CHECK(ServerMapModel::bestServer(servers) == 0);
}

TEST_CASE("AutopilotPlan maps profile flags to safe fix ids") {
    using namespace gno;
    GameProfile p;
    p.game_name = "test";
    p.power_plan_opt = true;
    p.game_dvr_opt = false;
    p.mtu_opt = false;
    p.tcp_opt = true;
    p.custom_dns = false;
    p.high_priority_opt = true;

    auto ids = AutopilotPlan::actionIdsFor(p);
    REQUIRE(ids.size() == 3);
    CHECK(ids[0] == "power_plan");
    CHECK(ids[1] == "tcp");
    CHECK(ids[2] == "priority");

    p.custom_dns = true;
    ids = AutopilotPlan::actionIdsFor(p);
    CHECK(std::find(ids.begin(), ids.end(), "dns") != ids.end());
}
TEST_CASE("NetUtils: probes, wlan parse, stun/dns codecs, mtu, bloat") {
    using namespace gno::netutils;

    HopStats s;
    summarizeProbes(s, {12.0, 14.0, -1, 16.0});
    CHECK(s.sent == 4);
    CHECK(s.lost == 1);
    CHECK(s.avg_ms == doctest::Approx(14.0));
    CHECK(s.jitter_ms == doctest::Approx(2.0));
    CHECK(std::string(hopVerdict(s)) == "BAD");

    const std::string sample =
        "SSID 1 : MyHome\n"
        "     BSSID 1 : aa:bb:cc:dd:ee:01\n"
        "     Channel : 6\n"
        "     Signal : 80%\n"
        "SSID 1 : MyHome\n"
        "     BSSID 1 : aa:bb:cc:dd:ee:02\n"
        "     Channel : 11\n"
        "     Signal : 40%\n";
    auto nets = parseNetshWlan(sample);
    REQUIRE(nets.size() == 2);
    CHECK(nets[0].channel == 6);
    CHECK(nets[0].bssid == "aa:bb:cc:dd:ee:01");
    CHECK(nets[1].rssi < nets[0].rssi);
    const int ch = bestChannel24(nets, -1);
    CHECK((ch == 1 || ch == 6 || ch == 11));

    std::uint8_t tid[12] = {1,2,3,4,5,6,7,8,9,10,11,12};
    auto req = buildBindingRequest(tid);
    CHECK(req.size() == 20);
    CHECK(req[0] == 0x00);
    CHECK(req[1] == 0x01);
    CHECK(req[4] == 0x21);
    CHECK(req[7] == 0x42);

    // Synthetic XOR-MAPPED-ADDRESS for 192.168.1.5:3074
    std::uint8_t resp[32] = {};
    resp[0]=0x01; resp[1]=0x01; resp[2]=0x00; resp[3]=0x0C;   // binding ok, msg len 12
    resp[20]=0x00; resp[21]=0x20;                              // XOR-MAPPED-ADDRESS
    resp[22]=0x00; resp[23]=0x08;                              // attr length 8
    resp[24]=0x00;                                             // zero byte
    resp[25]=0x01;                                             // family IPv4
    static const std::uint8_t M[4]={0x21,0x12,0xA4,0x42};
    resp[26]=0x0C^M[0]; resp[27]=0x02^M[1];                    // port 3074
    resp[28]=192^M[0]; resp[29]=168^M[1]; resp[30]=1^M[2]; resp[31]=5^M[3];
    std::string ip; std::uint16_t port=0;
    REQUIRE(parseXorMappedAddress(resp, sizeof(resp), ip, port));
    CHECK(ip == "192.168.1.5");
    CHECK(port == 3074);

    auto q = buildDnsQueryA("example.com", 0xABCD);
    CHECK(q.size() == 12 + 13 + 4);
    CHECK(q[0] == 0xAB);
    CHECK(q[1] == 0xCD);
    CHECK(q[2] == 0x01);

    std::uint8_t dns[45] = {};
    dns[5] = 1;
    dns[7] = 1;                       // ANCOUNT=1
    std::size_t o = 12; dns[o++] = 0; // root question name
    dns[o++]=0; dns[o++]=1; dns[o++]=0; dns[o++]=1;
    dns[o++]=0xC0; dns[o++]=12;       // pointer
    dns[o++]=0; dns[o++]=1; dns[o++]=0; dns[o++]=1;
    dns[o++]=0;dns[o++]=0;dns[o++]=0;dns[o++]=60;
    dns[o++]=0; dns[o++]=4;
    dns[o++]=1;dns[o++]=2;dns[o++]=3;dns[o++]=4;
    auto recs = parseDnsARecords(dns, sizeof(dns));
    REQUIRE(recs.size() == 1);
    CHECK(recs[0] == "1.2.3.4");

    int lo = 1000, hi = 1501;
    int mid = nextMtuProbe(lo, hi);
    hi = mid;                        // simulate fail at 1250
    mid = nextMtuProbe(lo, hi);      // ~1125
    CHECK(mid < 1250);

    CHECK(gradeBufferbloat(20, 22).tag == std::string("OK"));
    CHECK(gradeBufferbloat(20, 28).tag == std::string("MED"));
    CHECK(gradeBufferbloat(20, 50).tag == std::string("BAD"));

    // Качество сигнала — это проценты: на всём диапазоне RSSI укладываемся
    // в 0..100. Раньше linear-формула на (-40,-30) выдавала >100.
    CHECK(signalQuality(-95) == 0);
    CHECK(signalQuality(-90) == 0);
    CHECK(signalQuality(-60) == 60);
    CHECK(signalQuality(-35) <= 100);
    CHECK(signalQuality(-31) <= 100);
    CHECK(signalQuality(-30) == 100);
    CHECK(signalQuality(0) == 100);
}
TEST_CASE("ReportExporter builds valid JSON with escaping") {
    using namespace gno;
    auto json = ReportExporter::build("PUBG test", 23.5, 4.2, 0.1, 87,
                                      "Frankfurt", 18,
                                      {{"BEService", 0}, {"Vanguard", 2}});
    CHECK(json.find("\"quality_score\": 87") != std::string::npos);
    CHECK(json.find("\"severity\": 2") != std::string::npos);
}

TEST_CASE("ProfileEngine: INI roundtrip, corruption tolerance, unknown keys") {
    using namespace gno;

    GameProfile p;
    p.game_name = "Counter-Strike 2";
    p.process_name = "cs2";
    p.game_dvr_opt = false;
    p.custom_dns = true;
    p.dns_server = "9.9.9.9";
    auto doc = ProfileEngine::fromGameProfile(p, 55.0, 7.0, 1.5);

    auto ini = ProfileEngine::toIni(doc);
    CHECK(ini.rfind("GNO profile v1", 0) == 0);

    ProfileDocument back;
    REQUIRE(ProfileEngine::fromIni(ini, back));
    CHECK(back.game_name == "Counter-Strike 2");
    CHECK(back.process_name == "cs2");
    CHECK_FALSE(back.game_dvr);
    CHECK(back.custom_dns);
    CHECK(back.dns_server == "9.9.9.9");
    CHECK(back.max_median_rtt == doctest::Approx(55.0));
    CHECK(back.max_packet_loss == doctest::Approx(1.5));

    GameProfile restored;
    restored.mtu_opt = true;
    ProfileEngine::applyToGameProfile(back, restored);
    CHECK_FALSE(restored.game_dvr_opt);
    CHECK(restored.custom_dns);

    // Corrupted header must be rejected.
    ProfileDocument junk;
    CHECK_FALSE(ProfileEngine::fromIni("GNO profile v9\n", junk));
    CHECK_FALSE(ProfileEngine::fromIni("", junk));

    // Unknown keys ignored; partial file keeps defaults for the rest.
    ProfileDocument partial;
    REQUIRE(ProfileEngine::fromIni(
        "GNO profile v1\ngame=Valorant\nfuture_key=42\n", partial));
    CHECK(partial.game_name == "Valorant");
    CHECK(partial.tcp); // default
}
TEST_CASE("JsonBackupStore history is chronological, not id-ordered") {
    FakeStateApi api;
    auto dir = std::filesystem::temp_directory_path() / "gno_history_order";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    gno::remediation::JsonBackupStore store(dir.string());

    TransactionRecord old_rec{};
    old_rec.transaction_id = "tx-zzz-old";
    old_rec.status = gno::remediation::TransactionStatus::Applied;
    REQUIRE(store.save(old_rec).ok());
    // push its mtime 2 hours into the past
    const auto now_ft = std::filesystem::file_time_type::clock::now();
    std::filesystem::last_write_time(dir / "tx-zzz-old.json", now_ft - std::chrono::hours(2));

    TransactionRecord new_rec{};
    new_rec.transaction_id = "tx-000-new";
    new_rec.status = gno::remediation::TransactionStatus::Applied;
    REQUIRE(store.save(new_rec).ok());

    auto list = store.list();
    REQUIRE(list.ok());
    REQUIRE(list.value().size() == 2);
    CHECK(list.value().front().transaction_id == "tx-000-new"); // newer first despite smaller id
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("JsonBackupStore orders same-minute transactions by precise write time") {
    // Регресс: created_at имеет разрешение в минуту, поэтому две транзакции
    // внутри одной минуты сравнивались равными и std::sort (нестабильный) мог
    // поставить старую первой — «откатить последнее» откатывало не то.
    auto dir = std::filesystem::temp_directory_path() / "gno_history_same_minute";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    gno::remediation::JsonBackupStore store(dir.string());

    TransactionRecord older{};
    older.transaction_id = "tx-aaa-older";
    older.status = gno::remediation::TransactionStatus::Applied;
    REQUIRE(store.save(older).ok());

    TransactionRecord newer{};
    newer.transaction_id = "tx-zzz-newer";
    newer.status = gno::remediation::TransactionStatus::Applied;
    REQUIRE(store.save(newer).ok());

    // Обе в одной минуте: разница только в секундах.
    const auto now_ft = std::filesystem::file_time_type::clock::now();
    std::filesystem::last_write_time(dir / "tx-aaa-older.json", now_ft - std::chrono::seconds(40));
    std::filesystem::last_write_time(dir / "tx-zzz-newer.json", now_ft - std::chrono::seconds(10));

    auto list = store.list();
    REQUIRE(list.ok());
    REQUIRE(list.value().size() == 2);
    CHECK(list.value().front().transaction_id == "tx-zzz-newer");
    // Время теперь показывается с секундами, чтобы записи различались и в UI.
    CHECK(list.value().front().created_at.size() == 19);
    std::filesystem::remove_all(dir, ec);
}
TEST_CASE("CS2 maxping action flows through the transactional engine") {
    FakeStateApi api;
    MemoryBackupStore store;
    RemediationService service(api, store, testTargets);

    auto before = service.observeAll();
    REQUIRE(before.ok());
    const bool has_cs2 = before.value().size() > 8; // factory includes cs2 now

    auto applied = service.applyIds({ActionId::Cs2MaxPing});
    REQUIRE(applied.ok());
    CHECK(applied.value().succeeded);
    CHECK(api.cs2.at(1) == 60); // proposed default

    auto rolled = service.rollback(store.records.begin()->first);
    REQUIRE(rolled.ok());
}

TEST_CASE("AlertThresholds are stricter for tactical shooters") {
    using namespace gno;
    CHECK(AlertThresholds::forGame("VALORANT").max_ping_ms == 50);
    CHECK(AlertThresholds::forGame("Counter-Strike 2").max_ping_ms == 60);
    CHECK(AlertThresholds::forGame("PUBG: BATTLEGROUNDS").max_ping_ms == 100);
    CHECK(AlertThresholds::forGame("Something").max_ping_ms == 100);
    CHECK(AlertThresholds::forGame("VALORANT").isDegraded(55, 0));
}
TEST_CASE("GameMode and MouseAccel flow through the engine") {
    FakeStateApi api;
    MemoryBackupStore store;
    RemediationService service(api, store, testTargets);

    auto applied = service.applyIds({ActionId::GameMode, ActionId::MouseAccel});
    REQUIRE(applied.ok());
    CHECK(applied.value().succeeded);
    CHECK(api.registry[static_cast<int>(AllowedRegistryKey::GameModeAllow)].value == 0);
    CHECK(api.accel.speed == 0);

    auto rolled = service.rollback(store.records.begin()->first);
    REQUIRE(rolled.ok());
    CHECK(api.accel.speed == 1); // Windows default restored
}
// ============================ Gaming Health Score (spec 2026-09-15) =========
#include "core/health_score.h"

using namespace gno::health;

static NetworkInput netOK() {
    NetworkInput n; n.p50_ms = 34; n.jitter_p95_ms = 2.1; n.loss_pct = 0.0;
    n.dns_best_ms = 12; n.window_seconds = 60; n.nodes_probed = 7; n.nodes_total = 7;
    n.last_sample_age_seconds = 10; return n;
}

TEST_CASE("HealthScore: network scale anchors") {
    auto p = profileFor("CS2");
    NetworkInput n = netOK();
    // ping <= good*0.66 => 100 pts component; jitter 2ms boundary
    CHECK(networkScore(n, p) > 99.0);
    n.p50_ms = p.good_ping_ms * 3.0; // worst bound
    double s = networkScore(n, p);
    CHECK(s < 70.0); // loss/jitter/dns still perfect, ping weight .35
}

TEST_CASE("HealthScore: system penalty model") {
    SystemInput s; CHECK(systemScore(s) == 100.0);
    s.power_high_performance = false; s.gamedvr_disabled = false;
    CHECK(systemScore(s) == 75.0);
    s.disk_space_ok = false; s.fso_matches_profile = false;
    s.no_heavy_background = false; s.timer_resolution_ok = false;
    CHECK(systemScore(s) == 30.0); // 100-15-10-20-8-10-7
    s.power_high_performance = false; // already false; extra games etc stay
    CHECK(systemScore(s) >= 0.0);
}

TEST_CASE("HealthScore: renormalization when game ready & stability unknown") {
    auto p = profileFor("VALORANT");
    NetworkInput n = netOK(); n.p50_ms = 60; // valo good=50 -> penalty
    SystemInput s; s.gamedvr_disabled = false;
    GameReadyInput g; // not applicable
    StabilityInput st; // no history
    HealthResult r = compute(n, s, g, st, p);
    CHECK(r.game_ready < 0); CHECK(r.stability < 0);
    CHECK_FALSE(r.hidden_by_confidence);
    double manual = (networkScore(n, p) * 0.40 + systemScore(s) * 0.25) / 0.65;
    CHECK(std::abs(r.total - manual) < 0.01);
}

TEST_CASE("HealthScore: confidence hides number below 0.4") {
    auto p = profileFor("CS2");
    NetworkInput n = netOK(); n.window_seconds = 10; // 10/60 => .167
    HealthResult r = compute(n, SystemInput{}, GameReadyInput{}, StabilityInput{}, p);
    CHECK(r.confidence < 0.4);
    CHECK(r.hidden_by_confidence);
    CHECK(r.total < 0); // never render a fake number
    CHECK(std::string(levelName(r.level)) == "Unknown");
}

TEST_CASE("HealthScore: critical network gate overrides aggregate") {
    auto p = profileFor("CS2");
    NetworkInput n = netOK();
    n.p50_ms = 260; n.jitter_p95_ms = 25; n.loss_pct = 6.0; n.dns_best_ms = 200;
    HealthResult r = compute(n, SystemInput{}, GameReadyInput{}, StabilityInput{}, p);
    CHECK(r.network < 30.0);
    CHECK(r.critical_gate);
    CHECK(std::string(levelName(r.level)) == "Critical");
}

TEST_CASE("HealthScore: no network data => confidence zero, hidden") {
    NetworkInput n;
    HealthResult r = compute(n, SystemInput{}, GameReadyInput{}, StabilityInput{}, profileFor("PUBG"));
    CHECK(r.network < 0);
    CHECK(r.confidence == 0.0);
    CHECK(r.total < 0);
}

TEST_CASE("HealthScore: stability degradation lowers total") {
    auto p = profileFor("CS2");
    NetworkInput n = netOK();
    StabilityInput st; st.total_samples = 100; st.degraded_samples = 40;
    HealthResult r = compute(n, SystemInput{}, GameReadyInput{}, st, p);
    CHECK(std::abs(r.stability - 60.0) < 0.01);
    // Total sits below perfect network score: (100*.4+100*.25+60*.15)/.8 = 92.5
    CHECK(r.total < r.network);
    CHECK(std::abs(r.total - 92.5) < 0.5);
}

// ============================ BoostGate mapping (P0) =======================
#include "remediation/boost_gate.h"

TEST_CASE("BoostGate: request maps to engine actions only") {
    using namespace gno::remediation;
    BoostRequest req; // defaults: power/dvr/fso/mouse/priority on
    std::vector<std::string> skipped;
    auto ids = boostActionsFor(req, /*has_game=*/true, /*has_interface=*/true, &skipped);
    CHECK(std::find(ids.begin(), ids.end(), ActionId::PowerPlan) != ids.end());
    CHECK(std::find(ids.begin(), ids.end(), ActionId::GameDvr) != ids.end());
    CHECK(std::find(ids.begin(), ids.end(), ActionId::MouseAccel) != ids.end());
    CHECK(std::find(ids.begin(), ids.end(), ActionId::FullscreenOptimizations) != ids.end());
    CHECK(std::find(ids.begin(), ids.end(), ActionId::ProcessPriority) != ids.end());
    CHECK(skipped.empty());
}

TEST_CASE("BoostGate: no game -> target actions skipped with reason") {
    using namespace gno::remediation;
    BoostRequest req;
    std::vector<std::string> skipped;
    auto ids = boostActionsFor(req, /*has_game=*/false, /*has_interface=*/true, &skipped);
    CHECK(std::find(ids.begin(), ids.end(), ActionId::FullscreenOptimizations) == ids.end());
    CHECK(std::find(ids.begin(), ids.end(), ActionId::ProcessPriority) == ids.end());
    CHECK(skipped.size() == 2);
}

TEST_CASE("BoostGate: off-policy virtual memory is skipped, never applied") {
    using namespace gno::remediation;
    BoostRequest req;
    req.optimize_virtual_memory = true;
    std::vector<std::string> skipped;
    auto ids = boostActionsFor(req, true, true, &skipped);
    CHECK(ids.size() == 5); // no VM action exists in engine
    bool found = false;
    // Причина показывается пользователю как есть, поэтому в ней не должно быть
    // внутренних идентификаторов вида «VirtualMemory».
    for (auto& s : skipped) if (s.find("виртуальной памяти") != std::string::npos) found = true;
    CHECK(found);
    for (auto& s : skipped) CHECK(s.find("VirtualMemory") == std::string::npos);
}

TEST_CASE("BoostGate: custom DNS other than engine pair is refused") {
    using namespace gno::remediation;
    BoostRequest req;
    req.custom_dns = true; req.dns_server = "8.8.8.8";
    std::vector<std::string> skipped;
    auto ids = boostActionsFor(req, false, true, &skipped);
    CHECK(std::find(ids.begin(), ids.end(), ActionId::Dns) == ids.end());
    CHECK_FALSE(skipped.empty());
}

TEST_CASE("BoostGate: MTU only at engine-supported 1500") {
    using namespace gno::remediation;
    BoostRequest req;
    req.custom_dns = false; req.tcp_optimization = false;
    req.mtu_optimization = true; req.mtu_value = 1400;
    std::vector<std::string> skipped;
    auto ids = boostActionsFor(req, false, true, &skipped);
    CHECK(std::find(ids.begin(), ids.end(), ActionId::Mtu) == ids.end());
    req.mtu_value = 1500;
    skipped.clear();
    ids = boostActionsFor(req, false, true, &skipped);
    CHECK(std::find(ids.begin(), ids.end(), ActionId::Mtu) != ids.end());
}

TEST_CASE("BoostGate: default request does not diverge from the engine MTU value") {
    // Регресс: UI держал собственный дефолт 1400, из-за чего пункт «Оптимизация
    // MTU» с настройками по умолчанию молча отбрасывался движком.
    using namespace gno::remediation;
    CHECK(kEngineMtuValue == 1500u);
    BoostRequest req;
    CHECK(req.mtu_value == kEngineMtuValue);

    req.mtu_optimization = true;
    req.custom_dns = false;
    std::vector<std::string> skipped;
    const auto ids = boostActionsFor(req, /*has_game=*/false, /*has_interface=*/true, &skipped);
    const bool mtu_reached_engine = std::find(ids.begin(), ids.end(), ActionId::Mtu) != ids.end();
    CHECK(mtu_reached_engine);
}

TEST_CASE("BoostGate: DNS constant is the pair the engine accepts") {
    using namespace gno::remediation;
    BoostRequest req;
    req.custom_dns = true;
    CHECK(req.dns_server == kEngineDnsPrimary);
    std::vector<std::string> skipped;
    const auto ids = boostActionsFor(req, false, true, &skipped);
    CHECK(std::find(ids.begin(), ids.end(), ActionId::Dns) != ids.end());
}

TEST_CASE("BoostGate: empty request applies nothing and says so") {
    // Ничего не выбрано: движок не должен создать транзакцию и не должен
    // оставить маркер активного буста (иначе UI предложил бы пустой откат).
    using namespace gno::remediation;
    auto dir = std::filesystem::temp_directory_path() / "gno_boost_empty";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);

    BoostGate gate(dir.string());
    BoostRequest req;
    req.optimize_power_plan = false;
    req.disable_game_dvr = false;
    req.disable_fullscreen_optimizations = false;
    req.disable_mouse_acceleration = false;
    req.disable_game_mode = false;
    req.set_high_priority = false;
    req.custom_dns = false;
    req.tcp_optimization = false;
    req.mtu_optimization = false;
    req.optimize_virtual_memory = false;

    const auto result = gate.applyBoost(req);
    CHECK(result.applied == false);
    CHECK(result.ok);
    CHECK(result.transaction_id.empty());
    CHECK_FALSE(gate.hasActiveBoost());
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("BoostGate: revert without an active boost is a no-op") {
    using namespace gno::remediation;
    auto dir = std::filesystem::temp_directory_path() / "gno_boost_norevert";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);

    BoostGate gate(dir.string());
    CHECK_FALSE(gate.hasActiveBoost());
    const auto rolled = gate.revertBoost();
    CHECK(rolled.ok);
    CHECK(rolled.transaction_id.empty());
    std::filesystem::remove_all(dir, ec);
}

// ============================ ServerMap confidence (spec 1.7) ==============
#include "core/server_confidence.h"

TEST_CASE("ServerMap: empty window yields no-data") {
    using namespace gno::servermap;
    std::vector<ProbeSample> samples{{1000, 30}, {2000, 31}};
    auto e = estimate(samples, 10LL * 3600 * 1000); // all stale (>6h)
    CHECK_FALSE(e.valid);
    CHECK(e.confidence == Confidence::None);
}

TEST_CASE("ServerMap: below min samples -> low confidence, still shows value") {
    using namespace gno::servermap;
    const std::int64_t now = 100000;
    std::vector<ProbeSample> samples{{now - 1000, 34}, {now - 900, 35}};
    auto e = estimate(samples, now);
    CHECK(e.valid);
    CHECK(e.p50_ms >= 34);
    CHECK(e.confidence == Confidence::Low);
}

TEST_CASE("ServerMap: twelve stable samples -> high confidence") {
    using namespace gno::servermap;
    const std::int64_t now = 100000;
    std::vector<ProbeSample> samples;
    for (int i = 0; i < 12; ++i) samples.push_back({now - i * 100, 30.0 + (i % 2)});
    auto e = estimate(samples, now);
    CHECK(e.valid);
    CHECK(e.p50_ms >= 30); CHECK(e.p50_ms <= 31);
    CHECK(e.confidence == Confidence::High);
    CHECK(e.availability == 1.0);
}

TEST_CASE("ServerMap: packet loss lowers confidence") {
    using namespace gno::servermap;
    const std::int64_t now = 100000;
    std::vector<ProbeSample> samples;
    for (int i = 0; i < 12; ++i) samples.push_back({now - i * 100, 30.0});
    for (int i = 0; i < 8; ++i) samples.push_back({now - 2000 - i * 100, -1}); // failures
    auto e = estimate(samples, now);
    CHECK(e.availability < 0.8);
    CHECK(e.confidence != Confidence::High);
}

TEST_CASE("ServerMap: best server requires noise-floor margin") {
    using namespace gno::servermap;
    const std::int64_t now = 100000;
    std::vector<ProbeSample> a_samples, b_samples;
    for (int i = 0; i < 12; ++i) {
        a_samples.push_back({now - i * 100, 40.0 + (i % 3)});   // 40..42
        b_samples.push_back({now - i * 100, 41.0 + (i % 2)});   // 41..42, within a's noise
    }
    auto a = estimate(a_samples, now), b = estimate(b_samples, now);
    CHECK_FALSE(confidentBetter(a, b)); // difference within noise
    std::vector<ProbeSample> c_samples;
    for (int i = 0; i < 12; ++i) c_samples.push_back({now - i * 100, 20.0}); // clear winner
    auto c = estimate(c_samples, now);
    CHECK(confidentBetter(c, a));
    CHECK_FALSE(confidentBetter(a, c));
}

TEST_CASE("LaunchDiagnostics severity/counters invariants") {
    gno::GameDiagnostics d = gno::LaunchDiagnostics::run("InvariantTest", "definitely_not_running_xyz.exe");
    int passed = 0, warn = 0, err = 0;
    for (const auto& c : d.checks) {
        // успешная проверка не может иметь уровень warning/error,
        // упавшая — не может быть severity 0
        if (c.passed) CHECK(c.severity == 0);
        else CHECK(c.severity >= 1);
        if (c.severity == 2) ++err;
        else if (c.severity == 1) ++warn;
        else ++passed;
    }
    CHECK(d.passed_count == passed);
    CHECK(d.warning_count == warn);
    CHECK(d.error_count == err);
    const int total = passed + warn + err;
    CHECK(total == (int)d.checks.size());
}

// --- Regression: journal location -----------------------------------------
// Раньше было `getenv("APPDATA") ? getenv("APPDATA") : "."` — ПУСТОЕ значение
// считалось валидным, и журнал транзакций писался в текущий рабочий каталог.
// GUI и консоль могли получить два разных хранилища, и откат "не видел" буст.
#include "core/json_persistence.h"
#include "remediation/boost_gate.h"
#include <fstream>

TEST_CASE("appdata root survives a stripped environment") {
    const auto root = gno::persistence::applicationDataRoot();
    CHECK_FALSE(root.empty());
    CHECK(root.is_absolute());              // никогда не "." / относительный путь

    const auto backups = gno::remediation::defaultBackupsDir();
    CHECK(backups.find("GNO") != std::string::npos);
    CHECK(backups.find("Backups") != std::string::npos);
    CHECK(std::filesystem::path(backups).is_absolute());

    // Пустое значение переменной не должно побеждать реальный путь.
    const char* previous = std::getenv("APPDATA");
    const std::string saved = previous ? previous : "";
    _putenv_s("APPDATA", "");
    const auto root_with_empty_env = gno::persistence::applicationDataRoot();
    _putenv_s("APPDATA", saved.c_str());
    CHECK(root_with_empty_env.is_absolute());
    CHECK(root_with_empty_env == root);
}

TEST_CASE("BoostGate: revertable changes come from the journal, not only the marker") {
    // Регресс: одиночный фикс из «Диагностики» применялся через движок, но
    // маркер буста не ставился, поэтому кнопка «Откатить последние изменения»
    // в «Прайм-сетапе» оставалась серой при реально применённых изменениях.
    auto dir = std::filesystem::temp_directory_path() / "gno_gate_revertable";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);

    gno::remediation::BoostGate gate(dir.string());
    CHECK_FALSE(gate.hasActiveBoost());
    CHECK_FALSE(gate.hasRevertableChanges());
    CHECK(gate.latestAppliedTransactionId().empty());

    gno::remediation::JsonBackupStore store(dir.string());
    TransactionRecord older{};
    older.transaction_id = "tx-legacy-dns-1";
    older.status = gno::remediation::TransactionStatus::Applied;
    older.applied_action_order = {ActionId::Dns};
    REQUIRE(store.save(older).ok());

    TransactionRecord newest{};
    newest.transaction_id = "tx-legacy-tcp-2";
    newest.status = gno::remediation::TransactionStatus::Applied;
    newest.applied_action_order = {ActionId::TcpParameters};
    REQUIRE(store.save(newest).ok());

    TransactionRecord already_reverted{};
    already_reverted.transaction_id = "tx-legacy-old-0";
    already_reverted.status = gno::remediation::TransactionStatus::Reverted;
    REQUIRE(store.save(already_reverted).ok());

    // Самая свежая запись — «Applied», но список применённых действий пуст.
    // Движок такую откатить не может (FixTransaction::rollbackEligible), значит
    // и кнопка отката не должна её предлагать: иначе она обещала бы откат,
    // который движок отвергнет. Лежит первой по времени, поэтому проверка
    // «новейшая откатываемая = tx-legacy-tcp-2» ловит именно этот случай.
    TransactionRecord applied_but_empty{};
    applied_but_empty.transaction_id = "tx-legacy-empty-3";
    applied_but_empty.status = gno::remediation::TransactionStatus::Applied;
    REQUIRE(store.save(applied_but_empty).ok());

    // list() упорядочен по точному last_write_time; записи, созданные в одном
    // тике ФС, неразличимы, поэтому времена задаём явно (как в тестах выше).
    const auto now_ft = std::filesystem::file_time_type::clock::now();
    std::filesystem::last_write_time(dir / "tx-legacy-dns-1.json", now_ft - std::chrono::seconds(30));
    std::filesystem::last_write_time(dir / "tx-legacy-tcp-2.json", now_ft - std::chrono::seconds(5));
    std::filesystem::last_write_time(dir / "tx-legacy-old-0.json", now_ft - std::chrono::seconds(60));
    std::filesystem::last_write_time(dir / "tx-legacy-empty-3.json", now_ft - std::chrono::seconds(1));

    // Буста нет (маркера нет), но откатывать есть что.
    CHECK_FALSE(gate.hasActiveBoost());
    CHECK(gate.hasRevertableChanges());
    CHECK(gate.latestAppliedTransactionId() == "tx-legacy-tcp-2");

    // Маркер буста должен иметь приоритет над журналом.
    {
        std::ofstream marker(dir / "boost_transaction.txt", std::ios::trunc);
        marker << "tx-boost-9\n";
    }
    CHECK(gate.hasActiveBoost());
    CHECK(gate.hasRevertableChanges());
    CHECK(gate.latestAppliedTransactionId() == "tx-legacy-tcp-2");

    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("BoostGate: fully reverted journal has nothing to revert") {
    auto dir = std::filesystem::temp_directory_path() / "gno_gate_nothing";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);

    gno::remediation::JsonBackupStore store(dir.string());
    TransactionRecord rec{};
    rec.transaction_id = "tx-done";
    rec.status = gno::remediation::TransactionStatus::Reverted;
    REQUIRE(store.save(rec).ok());

    gno::remediation::BoostGate gate(dir.string());
    CHECK_FALSE(gate.hasRevertableChanges());
    CHECK(gate.latestAppliedTransactionId().empty());

    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("Правило отката одно на всех: движок, BoostGate и кнопка отката") {
    // Регресс: «Центр изменений» считал откатываемой только запись со статусом
    // Applied. Движок же откатывает и частично применённые (Failed/Cancelled с
    // непустым applied_action_order), поэтому такие изменения «залипали» —
    // пользователь видел «Ошибка» и не мог вернуть применённый префикс.
    using namespace gno::remediation;

    TransactionRecord applied{};
    applied.status = TransactionStatus::Applied;
    applied.applied_action_order = {ActionId::PowerPlan};
    CHECK(isRollbackEligible(applied));

    // Частичный успех: статус Applied (движок помечает так применённый префикс),
    // но одно из действий упало — откатывать всё равно можно и нужно.
    TransactionRecord partial = applied;
    ActionOutcome failed{};
    failed.id = ActionId::Mtu;
    failed.status = ActionStatus::Failed;
    failed.attempted = true;
    partial.outcomes = {failed};
    CHECK(isRollbackEligible(partial));
    CHECK(hasFailedOutcome(partial));
    CHECK_FALSE(hasFailedOutcome(applied));

    TransactionRecord legacy_failed{};
    legacy_failed.status = TransactionStatus::Failed;
    legacy_failed.applied_action_order = {ActionId::GameDvr};
    CHECK(isRollbackEligible(legacy_failed));

    // Откатывать нечего: пустой префикс или уже откаченная запись.
    TransactionRecord empty_applied{};
    empty_applied.status = TransactionStatus::Applied;
    CHECK_FALSE(isRollbackEligible(empty_applied));

    TransactionRecord reverted{};
    reverted.status = TransactionStatus::Reverted;
    reverted.applied_action_order = {ActionId::Dns};
    CHECK_FALSE(isRollbackEligible(reverted));

    TransactionRecord prepared{};
    prepared.status = TransactionStatus::Prepared;
    CHECK_FALSE(isRollbackEligible(prepared));
}

TEST_CASE("BoostGate: TCP-фикс не требует поднятого адаптера") {
    // Регресс: TcpInitialRtt — параметр HKLM\...\Tcpip\Parameters, адаптер ему
    // не нужен, но буст пропускал его при отсутствии активного интерфейса.
    // Dns/Mtu по-прежнему требуют адаптер: без него движку нечего менять.
    using namespace gno::remediation;
    BoostRequest req;
    req.optimize_power_plan = false;
    req.disable_game_dvr = false;
    req.disable_fullscreen_optimizations = false;
    req.disable_mouse_acceleration = false;
    req.disable_game_mode = false;
    req.set_high_priority = false;
    req.custom_dns = false;
    req.mtu_optimization = false;
    req.tcp_optimization = true;

    std::vector<std::string> skipped;
    const auto ids = boostActionsFor(req, /*has_game=*/false, /*has_interface=*/false, &skipped);
    const bool tcp_reached_engine = std::find(ids.begin(), ids.end(), ActionId::TcpParameters) != ids.end();
    CHECK(tcp_reached_engine);
    CHECK(skipped.empty());

    req.custom_dns = true;
    req.mtu_optimization = true;
    std::vector<std::string> skipped_net;
    const auto net_ids = boostActionsFor(req, false, /*has_interface=*/false, &skipped_net);
    const bool dns_reached = std::find(net_ids.begin(), net_ids.end(), ActionId::Dns) != net_ids.end();
    const bool mtu_reached = std::find(net_ids.begin(), net_ids.end(), ActionId::Mtu) != net_ids.end();
    CHECK_FALSE(dns_reached);
    CHECK_FALSE(mtu_reached);
    CHECK(skipped_net.size() == 2);
}

TEST_CASE("Резолвер целей по умолчанию не роняет движок") {
    // Регресс: у RemediationService был резолвер-заглушка, отдававший NoTarget
    // всем действиям. Dns/Mtu читают цель через std::get<InterfaceTarget>, так
    // что вызов observeAll()/applyAll() без явного резолвера бросал
    // bad_variant_access. Проверяем, что дефолтный резолвер — настоящий.
    using namespace gno::remediation;
    WindowsStateApi api;
    JsonBackupStore store(
        (std::filesystem::temp_directory_path() / "gno_default_resolver").string());
    RemediationService service(api, store);

    const auto observed = service.observeAll();
    REQUIRE(observed.ok());
    // Одно представление на каждое действие движка (createActions).
    CHECK(observed.value().size() == 10);
    for (const auto& view : observed.value()) CHECK_FALSE(view.display_name.empty());
}

TEST_CASE("BoostGate: неудачный откат не снимает маркер активного буста") {
    // Маркер — единственный признак «буст включён» для трея. Снимать его можно
    // только после успешного отката: иначе трей показывал бы выключенный буст
    // при реально применённых изменениях.
    auto dir = std::filesystem::temp_directory_path() / "gno_gate_marker_keep";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    {
        std::ofstream marker(dir / "boost_transaction.txt", std::ios::trunc);
        marker << "tx-missing-1\n";
    }
    gno::remediation::BoostGate gate(dir.string());
    CHECK(gate.hasActiveBoost());

    // Такой транзакции в журнале нет — откат обязан провалиться.
    const auto rolled = gate.revertTransaction("tx-missing-1");
    CHECK_FALSE(rolled.ok);
    CHECK(rolled.transaction_id == "tx-missing-1");
    CHECK(gate.hasActiveBoost());

    // Пустой id — это «откатывать нечего», а не ошибка, и маркер тоже цел.
    const auto nothing = gate.revertTransaction("");
    CHECK(nothing.ok);
    CHECK(nothing.transaction_id.empty());
    CHECK(gate.hasActiveBoost());

    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("applyFixDetailed distinguishes 'applied' from 'just a hint'") {
    // Регресс: автопилот игнорировал результат applyFix и всегда сообщал
    // «профиль применён (с бэкапом)», даже когда движок ничего не сделал.
    // Теперь успех — машинно-читаемый признак, а не догадка по тексту.

    // Намеренный отказ вне белого списка: не ошибка, но и не применение.
    const auto vm = gno::LaunchDiagnostics::applyFixDetailed("vm");
    CHECK_FALSE(vm.applied);
    CHECK(vm.informational);
    CHECK_FALSE(vm.message.empty());

    // Неизвестный id: это уже ошибка, а не подсказка.
    const auto unknown = gno::LaunchDiagnostics::applyFixDetailed("definitely_not_an_action");
    CHECK_FALSE(unknown.applied);
    CHECK_FALSE(unknown.informational);
    CHECK(unknown.message.find("Неизвестное") != std::string::npos);

    // applyFix остаётся тонкой обёрткой над подробной версией.
    CHECK(gno::LaunchDiagnostics::applyFix("vm") == vm.message);
}

TEST_CASE("SystemAudit scan returns sane results for the auto-scan option") {
    // Галочка «Автоскан при старте» раньше только показывала сообщение и ничего
    // не проверяла. Теперь она опирается на эти функции — убеждаемся, что они
    // действительно что-то возвращают и что статусы не выдуманы.
    const auto fps = gno::SystemAudit::verifyFpsSettings();
    const auto net = gno::SystemAudit::verifyNetworkSettings();
    CHECK_FALSE(fps.empty());
    CHECK_FALSE(net.empty());

    int checked = 0;
    for (const auto* list : {&fps, &net}) {
        for (const auto& c : *list) {
            ++checked;
            CHECK_FALSE(c.section.empty());
            CHECK_FALSE(c.name.empty());
            CHECK_FALSE(c.detail.empty());
            const bool known = c.status == gno::SettingChange::Status::Verified ||
                               c.status == gno::SettingChange::Status::NotApplied ||
                               c.status == gno::SettingChange::Status::Failed ||
                               c.status == gno::SettingChange::Status::Applied ||
                               c.status == gno::SettingChange::Status::AdminRequired;
            CHECK(known);
        }
    }
    CHECK(checked >= 2);
}

// --- SessionHistory: регрессии по «замиранию» приложения ---------------------
// До фикса recordEnd() удерживал mutex_ и вызывал saveToFile(), который
// захватывал тот же самый нерекурсивный std::mutex. Поток вис навсегда, а
// вместе с ним — главный поток: приложение переставало отвечать, не шли
// таймеры и не срабатывал планировщик. Тест сторожевой: работа выполняется в
// отдельном потоке, поэтому регресс проявится как таймаут, а не как вечное
// ожидание всего набора тестов.
TEST_CASE("SessionHistory: recordEnd и clear не блокируют поток навсегда") {
    const auto path = std::filesystem::temp_directory_path() / "gno_hist_deadlock.json";
    std::filesystem::remove(path);

    // Намеренно new: при регрессе рабочий поток остаётся заблокированным внутри
    // объекта, и удалять его нельзя.
    auto* h = new SessionHistory(path.string());
    std::atomic<bool> done{false};
    std::thread worker([h, &done]() {
        h->recordStart("scheduled-evening", false);
        h->recordEnd(20.0, 3.0, 0.0, 30.0);
        h->clear();
        done.store(true);
    });

    for (int i = 0; i < 300 && !done.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    const bool finished = done.load();
    if (finished) {
        worker.join();
        delete h;
    } else {
        worker.detach();
    }
    CHECK(finished);

    std::filesystem::remove(path);
}

TEST_CASE("SessionHistory: сессия сохраняется на диск после recordEnd") {
    const auto path = std::filesystem::temp_directory_path() / "gno_hist_save.json";
    std::filesystem::remove(path);
    {
        SessionHistory h(path.string());
        h.recordStart("test-game", true);
        h.recordEnd(25.0, 4.0, 1.5, 40.0);
    }

    SessionHistory loaded(path.string());
    const auto all = loaded.getAll();
    REQUIRE(all.size() == 1);
    CHECK(all[0].game_name == "test-game");
    CHECK(all[0].boost_was_active == true);
    CHECK(all[0].avg_ping_ms == doctest::Approx(25.0));
    CHECK(all[0].avg_jitter_ms == doctest::Approx(4.0));
    CHECK(all[0].avg_packet_loss == doctest::Approx(1.5));
    // max_ping и duration тоже должны пережить перезапуск: раньше они
    // записывались в файл, но при чтении не разбирались и обнулялись.
    CHECK(all[0].max_ping_ms == doctest::Approx(40.0));

    std::filesystem::remove(path);
}

TEST_CASE("SessionHistory: оценка не приписывается чужой сессии") {
    const auto path = std::filesystem::temp_directory_path() / "gno_hist_score.json";
    std::filesystem::remove(path);
    {
        SessionHistory h(path.string());
        h.recordStart("cs2", false);
        h.recordEndWithScore(10.0, 1.0, 0.0, 12.0, 90.0);

        // Второй вызов без recordStart() не должен ничего менять. Раньше
        // recordEndWithScore() присваивал score полю records_.back() и портил
        // оценку уже сохранённой сессии.
        h.recordEndWithScore(999.0, 999.0, 99.0, 999.0, 1.0);

        const auto all = h.getAll();
        REQUIRE(all.size() == 1);
        CHECK(all[0].avg_ping_ms == doctest::Approx(10.0));
        CHECK(all[0].quality_score == doctest::Approx(90.0));
    }
    std::filesystem::remove(path);
}

TEST_CASE("SessionHistory: средние значения считаются по всем сессиям") {
    const auto path = std::filesystem::temp_directory_path() / "gno_hist_avg.json";
    std::filesystem::remove(path);
    {
        SessionHistory h(path.string());
        h.recordStart("a", false);
        h.recordEnd(10.0, 2.0, 1.0, 15.0);
        h.recordStart("b", false);
        h.recordEnd(30.0, 6.0, 3.0, 40.0);

        CHECK(h.getAveragePing() == doctest::Approx(20.0));
        CHECK(h.getAverageJitter() == doctest::Approx(4.0));
        // Средние потери раньше не считались вообще: сводная оценка на странице
        // «История» подставляла потери = 0 и расходилась с оценками сессий.
        CHECK(h.getAverageLoss() == doctest::Approx(2.0));
    }
    std::filesystem::remove(path);
}

TEST_CASE("Проверки читают те же параметры, которые пишет движок") {
    // Раньше SystemAudit читал собственные пути реестра, и они разошлись с
    // движком: GameMode проверялся по AutoGameModeEnabled (движок пишет
    // AllowAutoGameMode), TCP — по TcpAckFrequency/TCPNoDelay (движок пишет
    // TcpInitialRtt), а полноэкранные оптимизации — по GameDVR_FSEBehaviorMode,
    // которого движок вообще не трогает. Из-за этого вкладка «Что изменилось»
    // показывала «не применилось» сразу после успешного применения.
    auto text = [](const std::vector<gno::SettingChange>& v) {
        std::string all;
        for (const auto& c : v) all += c.name + "|" + c.old_value + "|" + c.detail + "\n";
        return all;
    };

    const std::string net = text(gno::SystemAudit::verifyNetworkSettings());
    CHECK(net.find("TcpInitialRtt") != std::string::npos);
    CHECK(net.find("TcpAckFrequency") == std::string::npos);
    CHECK(net.find("TCPNoDelay") == std::string::npos);
    // DNS и MTU движок тоже применяет — они должны попадать в проверку.
    CHECK(net.find("DNS") != std::string::npos);
    CHECK(net.find("MTU") != std::string::npos);

    const std::string fps = text(gno::SystemAudit::verifyFpsSettings());
    CHECK(fps.find("GameDVR_Enabled") != std::string::npos);
    CHECK(fps.find("AllowAutoGameMode") != std::string::npos);
    CHECK(fps.find("GameDVR_FSEBehaviorMode") == std::string::npos);
    CHECK(fps.find("AppCaptureEnabled") == std::string::npos);

    // Ожидаемые значения в UI сравниваются с выводом summarizeValue, поэтому
    // формат обязан остаться прежним, иначе строки молча станут «не применилось».
    CHECK(gno::remediation::summarizeValue(gno::remediation::MtuValue{1500}) == "1500 байт");
    gno::remediation::DnsValue dns;
    dns.automatic = false;
    dns.servers = {"1.1.1.1", "1.0.0.1"};
    CHECK(gno::remediation::summarizeValue(gno::remediation::ActionValue{dns}) ==
          "1.1.1.1, 1.0.0.1");

    // Значения движка живут в одном месте: UI и движок берут их отсюда.
    CHECK(gno::remediation::kEngineTcpInitialRtt == 5000);
    CHECK(std::string(gno::remediation::kEngineFullscreenFlags)
              .find("DISABLEDXMAXIMIZEDWINDOWEDMODE") != std::string::npos);
}

TEST_CASE("Одно не применившееся действие не отменяет остальные") {
    // Раньше FixTransaction::execute() выходил на первой же ошибке: MTU без
    // прав администратора останавливал транзакцию, и все следующие пункты
    // (например, ускорение мыши) не выполнялись вовсе, но UI показывал их как
    // «не применилось». Теперь неудача одного действия не мешает другим, а
    // применённый префикс остаётся откатываемым.
    using namespace gno::remediation;
    FakeStateApi api;
    MemoryBackupStore store;
    const InterfaceTarget iface{"{IF-GUID}", 7};
    api.mtu[iface.id] = MtuValue{1500};   // значение из белого списка
    api.fail_for.insert(ActionId::Mtu);   // как netsh без прав администратора

    RemediationService service(api, store, [&](const std::vector<FixAction*>& actions) {
        std::vector<std::optional<ActionTarget>> targets;
        for (auto* a : actions) {
            if (a->id() == ActionId::Mtu) targets.push_back(ActionTarget{iface});
            else targets.push_back(ActionTarget{NoTarget{}});
        }
        return targets;
    });

    auto res = service.applyIds({ActionId::PowerPlan, ActionId::Mtu, ActionId::MouseAccel});
    const auto& outcome = res.value();
    CHECK_FALSE(outcome.succeeded);   // общий итог — с ошибкой, это честно

    auto status_of = [&](ActionId id) {
        for (const auto& o : outcome.outcomes)
            if (o.id == id) return o.status;
        return ActionStatus::NotChecked;
    };
    CHECK(status_of(ActionId::PowerPlan) == ActionStatus::Applied);
    CHECK(status_of(ActionId::MouseAccel) == ActionStatus::Applied);
    CHECK(status_of(ActionId::Mtu) == ActionStatus::Failed);

    // Запись помечена Applied, иначе применённый префикс нельзя было бы откатить.
    CHECK(outcome.status == TransactionStatus::Applied);
    CHECK(api.plan.identifier == "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c");
    CHECK(api.accel.speed == 0);
}

TEST_CASE("Канонические строки состояния совпадают с тем, что отдаёт SystemAudit") {
    // UI сравнивает прочитанное значение с ожидаемым литералом. Если строки
    // разойдутся, строка «Что изменилось» навсегда останется «не применилось»,
    // поэтому обе стороны берут их из одной константы.
    CHECK(std::string(gno::kAuditOffText) == "отключено");
    CHECK(std::string(gno::kAuditOnText) == "включено");

    // И SystemAudit действительно возвращает именно эти строки, когда движок
    // сообщает «выключено» — на любой машине это либо они, либо пустая строка
    // (когда прочитать не удалось), но не мусор в другой кодировке.
    const std::string accel = gno::SystemAudit::readMouseAccelValue();
    const bool accel_ok = accel.empty() || accel == std::string(gno::kAuditOffText) ||
                          accel == std::string(gno::kAuditOnText);
    CHECK(accel_ok);
    const std::string fso = gno::SystemAudit::readFullscreenOptValue();
    const bool fso_ok = fso.empty() || fso == std::string(gno::kAuditOffText) ||
                        fso == std::string(gno::kAuditOnText);
    CHECK(fso_ok);
}

// ---------------------------------------------------------------------------
// diagnostics/diagnostic_types: Ipv4Address + CancellationSource.
// Живая часть слоя: парсер IP используется пинг-монитором для валидации цели,
// а CancellationSource — пинг-монитором и GameWatcher. Тестов не было вообще.
// ---------------------------------------------------------------------------

TEST_CASE("Ipv4Address::parse принимает корректные адреса") {
    CHECK(Ipv4Address::parse("0.0.0.0").has_value());
    CHECK(Ipv4Address::parse("127.0.0.1").has_value());
    CHECK(Ipv4Address::parse("192.168.1.1").has_value());
    CHECK(Ipv4Address::parse("255.255.255.255").has_value());
    CHECK(Ipv4Address::parse("10.0.0.255").has_value());
    CHECK(Ipv4Address::parse("1.2.3.4").has_value());
}

TEST_CASE("Ipv4Address::parse отклоняет мусор") {
    CHECK_FALSE(Ipv4Address::parse("").has_value());       // пусто
    CHECK_FALSE(Ipv4Address::parse("1.2.3").has_value());   // мало октетов
    CHECK_FALSE(Ipv4Address::parse("1.2.3.4.5").has_value());// лишний октет
    CHECK_FALSE(Ipv4Address::parse("1.2.3.").has_value());  // висящая точка
    CHECK_FALSE(Ipv4Address::parse(".1.2.3").has_value());  // точка спереди
    CHECK_FALSE(Ipv4Address::parse("1..2.3").has_value());  // пустой октет
    CHECK_FALSE(Ipv4Address::parse("256.1.1.1").has_value());// октет > 255
    CHECK_FALSE(Ipv4Address::parse("1.2.3.300").has_value());
    CHECK_FALSE(Ipv4Address::parse("999.999.999.999").has_value());
    CHECK_FALSE(Ipv4Address::parse("1.2.3.4 ").has_value());// хвостовой пробел
    CHECK_FALSE(Ipv4Address::parse(" 1.2.3.4").has_value());
    CHECK_FALSE(Ipv4Address::parse("1.2.3.x").has_value()); // не-цифра
    CHECK_FALSE(Ipv4Address::parse("abc").has_value());
    CHECK_FALSE(Ipv4Address::parse("1.2.3.-1").has_value());// минус не цифра
}

TEST_CASE("Ipv4Address::parse хранит правильные байты") {
    auto a = Ipv4Address::parse("192.168.0.1");
    REQUIRE(a.has_value());
    const auto bytes = a->bytes();
    CHECK(bytes[0] == 192);
    CHECK(bytes[1] == 168);
    CHECK(bytes[2] == 0);
    CHECK(bytes[3] == 1);
    CHECK(a->toString() == "192.168.0.1");
}

TEST_CASE("Ipv4Address round-trip через toString") {
    for (const char* text : {"0.0.0.0", "127.0.0.1", "10.20.30.40", "255.255.255.255", "8.8.8.8"}) {
        const auto parsed = Ipv4Address::parse(text);
        REQUIRE(parsed.has_value());
        CHECK(parsed->toString() == std::string(text));
        // повторный парс строки-результата должен давать тот же адрес
        const auto again = Ipv4Address::parse(parsed->toString());
        REQUIRE(again.has_value());
        CHECK(*again == *parsed);
    }
}

TEST_CASE("Ipv4Address::isUnspecified") {
    CHECK(Ipv4Address{}.isUnspecified());
    CHECK(Ipv4Address::parse("0.0.0.0")->isUnspecified());
    CHECK_FALSE(Ipv4Address::parse("0.0.0.1")->isUnspecified());
    CHECK_FALSE(Ipv4Address::parse("1.2.3.4")->isUnspecified());
}

TEST_CASE("CancellationSource: токен по умолчанию не отменён") {
    CancellationSource source;
    CHECK_FALSE(source.token().isCancelled());
}

TEST_CASE("CancellationSource: cancel() виден всем копиям токена") {
    CancellationSource source;
    const auto token_a = source.token();
    const auto token_b = source.token(); // второй токен делит то же состояние
    CHECK_FALSE(token_a.isCancelled());
    CHECK_FALSE(token_b.isCancelled());

    source.cancel();
    CHECK(token_a.isCancelled());
    CHECK(token_b.isCancelled());
}

TEST_CASE("CancellationSource: копия разделяет состояние отмены") {
    CancellationSource source;
    const auto token = source.token();
    CancellationSource copy = source; // copy ctor — разделяют состояние
    copy.cancel();
    CHECK(token.isCancelled());
}

TEST_CASE("CancellationSource: перемещённый источник инертен") {
    CancellationSource source;
    const auto token = source.token();
    CancellationSource moved = std::move(source); // состояние теперь у moved

    // moved-from источник не должен отменять токен и не должен падать
    source.cancel();
    CHECK_FALSE(token.isCancelled());
    // и не должен раздавать «отменённые» токены
    CHECK_FALSE(source.token().isCancelled());

    moved.cancel();
    CHECK(token.isCancelled());
}

TEST_CASE("CancellationSource: токен переживает уничтожение источника") {
    // Состояние живёт в shared_ptr, поэтому токен, выданный до уничтожения
    // источника, остаётся валидным — на это полагаются Watcher/PingMonitor.
    CancellationToken token;
    {
        CancellationSource source;
        token = source.token();
        CHECK_FALSE(token.isCancelled());
        source.cancel();
        CHECK(token.isCancelled());
    }
    CHECK(token.isCancelled());
}

// ---------------------------------------------------------------------------
// Реализация слоя diagnostics: EndpointObserver / NetworkSampler / ProbeClient
// ---------------------------------------------------------------------------

TEST_CASE("EndpointObserver: разбор соединений процесса") {
    // Фейковый провайдер имитирует вывод remoteTcpEndpointsForPid.
    EndpointObserver observer([](std::uint32_t pid) {
        REQUIRE(pid == 4242);
        return std::vector<std::string>{
            "93.184.216.34:27015",
            "127.0.0.1:1234",     // unspecified-loopback отбрасывается? нет — разберём
            "garbage",            // мусор: нет порта
            "10.0.0.1:notanumber",// мусор: порт не число
            "1.1.1.1:443",
        };
    });

    const CancellationToken cancellation;
    const auto result = observer.observe(4242, std::chrono::seconds(1), cancellation);
    CHECK(result.ok());
    REQUIRE(result.value.size() >= 2);

    bool found_game_server = false;
    bool found_dns = false;
    for (const auto& endpoint : result.value) {
        if (endpoint.port == 27015 && endpoint.ip.toString() == "93.184.216.34") {
            found_game_server = true;
            CHECK(endpoint.owner_pid == 4242);
        }
        if (endpoint.port == 443 && endpoint.ip.toString() == "1.1.1.1") found_dns = true;
    }
    CHECK(found_game_server);
    CHECK(found_dns);
}

TEST_CASE("EndpointObserver: мусорные адреса отбрасываются") {
    EndpointObserver observer([](std::uint32_t) {
        return std::vector<std::string>{"garbage", "1.2.3.4.5:80", ":80", "1.1.1.1:"};
    });
    const CancellationToken cancellation;
    const auto result = observer.observe(1, std::chrono::seconds(1), cancellation);
    CHECK_FALSE(result.ok());
    CHECK(result.error == DiagnosticError::EndpointNotObserved);
}

TEST_CASE("EndpointObserver: pid=0 — игра не обнаружена") {
    EndpointObserver observer;
    const CancellationToken cancellation;
    const auto result = observer.observe(0, std::chrono::seconds(1), cancellation);
    CHECK_FALSE(result.ok());
    CHECK(result.error == DiagnosticError::GameNotDetected);
}

namespace {

// Синтетический источник проб для NetworkSampler: раздаёт заготовленные
// задержки, а потом сообщает Exhausted — цикл останавливается детерминированно.
// Отрицательное значение в очереди = потерянный пакет.
NetworkSampler::Probe makeProbe(std::vector<double> samples) {
    const auto queue = std::make_shared<std::vector<double>>(std::move(samples));
    return [queue](const Ipv4Address&, std::uint32_t, double& latency_ms) {
        if (queue->empty()) return NetworkSampler::ProbeOutcome::Exhausted;
        const double value = queue->back();
        queue->pop_back();
        if (value < 0.0) return NetworkSampler::ProbeOutcome::NoReply;
        latency_ms = value;
        return NetworkSampler::ProbeOutcome::Replied;
    };
}

SampleTarget targetOf(const std::string& ip, std::uint16_t port = 27015) {
    SampleTarget target;
    target.ip = Ipv4Address::parse(ip).value_or(Ipv4Address{});
    target.port = port;
    target.protocol = TransportProtocol::Udp;
    return target;
}

} // namespace

TEST_CASE("NetworkSampler: статистика по синтетическим пробам") {
    // 10 проб: 30,31,32,33,34,35,36,37,38,39 (одна потеря)
    NetworkSampler sampler(makeProbe({39, 38, 37, -1, 36, 35, 34, 33, 32, 30}));
    SamplePlan plan;
    plan.duration_seconds = 60; // реальную длительность заменяет Exhausted источника
    plan.interval_ms = 0;
    plan.timeout_ms = 100;

    const CancellationToken cancellation;
    const auto result = sampler.sample(targetOf("1.1.1.1"), plan, cancellation);
    REQUIRE(result.ok());
    const auto& m = result.value;
    CHECK(m.sent == 10);
    CHECK(m.received == 9);
    CHECK(m.loss_percent == doctest::Approx(10.0).epsilon(0.01));
    // медиана 9 отсортированных ответов [30..39] (без одной потери) = 35
    CHECK(m.median_ms == doctest::Approx(35.0));
    CHECK(m.p95_ms >= m.median_ms);
    CHECK(m.jitter_ms > 0.0); // соседние отсчёты различаются
}

TEST_CASE("NetworkSampler: менее 3 ответов — недостаточно данных") {
    NetworkSampler sampler(makeProbe({30.0, 31.0}));
    SamplePlan plan;
    plan.duration_seconds = 60; // цикл остановит Exhausted, а не таймер
    plan.interval_ms = 0;
    const CancellationToken cancellation;
    const auto result = sampler.sample(targetOf("1.1.1.1"), plan, cancellation);
    CHECK_FALSE(result.ok());
    CHECK(result.error == DiagnosticError::InsufficientResponses);
    CHECK(result.value.received == 2);
}

TEST_CASE("NetworkSampler: отмена прерывает цикл") {
    NetworkSampler sampler(makeProbe({30.0, 31.0}));
    SamplePlan plan;
    plan.duration_seconds = 60; // без отмены цикл завис бы на источнике
    plan.interval_ms = 0;
    CancellationSource source;
    source.cancel(); // отменяем заранее
    const auto result = sampler.sample(targetOf("1.1.1.1"), plan, source.token());
    CHECK_FALSE(result.ok());
    CHECK(result.error == DiagnosticError::Cancelled);
}

TEST_CASE("NetworkSampler: unspecified-цель отклоняется") {
    NetworkSampler sampler;
    SamplePlan plan;
    plan.duration_seconds = 1;
    plan.interval_ms = 0;
    const CancellationToken cancellation;
    const auto result = sampler.sample(targetOf("0.0.0.0", 0), plan, cancellation);
    CHECK_FALSE(result.ok());
    CHECK(result.error == DiagnosticError::UnsupportedCapability);
}

TEST_CASE("ProbeClient: эталоны отвечают — best latency выбирается") {
    // Первый эталон «ответит» 12 мс, второй 4 мс — должен быть выбран второй.
    ProbeClient client([](const std::string& ip, std::uint32_t, double& latency_ms) {
        if (ip == "8.8.8.8") { latency_ms = 4.0; return true; }
        if (ip == "1.1.1.1") { latency_ms = 12.0; return true; }
        return false;
    });
    ProbeRequest request;
    request.game_id = "CS2";
    request.duration_seconds = 1;
    const CancellationToken cancellation;
    const auto result = client.measure(request, cancellation);
    REQUIRE(result.ok());
    CHECK(result.value.probe_region == "Google 8.8.8.8");
    CHECK(result.value.client_to_probe.median_ms == doctest::Approx(4.0));
}

TEST_CASE("ProbeClient: эталоны недоступны") {
    ProbeClient client([](const std::string&, std::uint32_t, double&) { return false; });
    ProbeRequest request;
    request.duration_seconds = 1;
    const CancellationToken cancellation;
    const auto result = client.measure(request, cancellation);
    CHECK_FALSE(result.ok());
    CHECK(result.error == DiagnosticError::ProbeUnavailable);
}

// ---------------------------------------------------------------------------
// DiagnosticsService: классификация диагноза на инжектированных зависимостях
// ---------------------------------------------------------------------------

namespace {

// Удобный сборщик fake-зависимостей сервиса.
struct FakeDeps {
    std::vector<std::string> endpoints;      // соединения "ip:port"
    std::vector<double> direct_samples;      // пробы до сервера игры
    std::vector<double> gateway_samples;     // пробы до шлюза
    std::vector<double> probe_samples;       // пробы до эталонов
    std::string gateway_ip = "192.168.1.1";
    std::uint32_t game_pid = 777;
    bool probe_available = true;

    DiagnosticsService::Dependencies toDeps() {
        DiagnosticsService::Dependencies deps;
        deps.endpoint_observer = std::make_unique<EndpointObserver>(
            [this](std::uint32_t pid) {
                REQUIRE(pid == game_pid);
                return endpoints;
            });
        deps.network_sampler = std::make_unique<NetworkSampler>([this](const Ipv4Address& target,
                                                                       std::uint32_t, double& latency_ms) {
            const std::string text = target.toString();
            std::vector<double>* queue = nullptr;
            if (text == gateway_ip) queue = &gateway_samples;
            else queue = &direct_samples;
            if (queue->empty()) return NetworkSampler::ProbeOutcome::Exhausted;
            const double value = queue->back();
            queue->pop_back();
            if (value < 0.0) return NetworkSampler::ProbeOutcome::NoReply;
            latency_ms = value;
            return NetworkSampler::ProbeOutcome::Replied;
        });
        deps.probe_client = std::make_unique<ProbeClient>(
            [this](const std::string&, std::uint32_t, double& latency_ms) {
                if (!probe_available || probe_samples.empty()) return false;
                latency_ms = probe_samples.back();
                probe_samples.pop_back();
                return true;
            },
            std::vector<ProbeClient::ReferenceEndpoint>{{"Test", "1.1.1.1"}});
        deps.game_locator = [this](const std::string&) { return std::optional<std::uint32_t>(game_pid); };
        deps.gateway_resolver = [this](const std::string&) {
            return std::optional<std::string>(gateway_ip);
        };
        return deps;
    }
};

std::vector<double> goodSamples() { return {30.0, 31.0, 29.0}; }
std::vector<double> badLatencySamples() { return {150.0, 160.0, 155.0}; }

} // namespace

TEST_CASE("DiagnosticsService: игра не запущена") {
    FakeDeps fake;
    auto deps = fake.toDeps();
    deps.game_locator = [](const std::string&) { return std::optional<std::uint32_t>{}; };
    DiagnosticsService service(std::move(deps));

    ProbeRequest request;
    request.game_id = "CS2";
    const CancellationToken cancellation;
    const auto result = service.run(request, cancellation);
    CHECK_FALSE(result.ok());
    CHECK(result.error == DiagnosticError::GameNotDetected);
}

TEST_CASE("DiagnosticsService: нет соединений у процесса") {
    FakeDeps fake; // endpoints пустые
    DiagnosticsService service(fake.toDeps());

    ProbeRequest request;
    request.game_id = "CS2";
    const CancellationToken cancellation;
    const auto result = service.run(request, cancellation);
    CHECK_FALSE(result.ok());
    CHECK(result.error == DiagnosticError::EndpointNotObserved);
}

TEST_CASE("DiagnosticsService: маршрут в норме") {
    FakeDeps fake;
    fake.endpoints = {"93.184.216.34:27015"};
    fake.direct_samples = goodSamples();
    fake.gateway_samples = goodSamples();
    DiagnosticsService service(fake.toDeps());

    ProbeRequest request;
    request.game_id = "CS2";
    request.duration_seconds = 1;
    const CancellationToken cancellation;
    const auto result = service.run(request, cancellation);
    REQUIRE(result.ok());
    CHECK(result.value.outcome == DiagnosticOutcome::NoImprovementFound);
    CHECK(result.value.confidence == ConfidenceLevel::High);
    CHECK(result.value.endpoint.port == 27015);
    CHECK_FALSE(result.value.evidence.empty());
}

TEST_CASE("DiagnosticsService: проблема в локальной сети (шлюз плох)") {
    FakeDeps fake;
    fake.endpoints = {"93.184.216.34:27015"};
    fake.direct_samples = badLatencySamples();
    fake.gateway_samples = {90.0, 95.0, 100.0}; // шлюз хуже 80 мс
    DiagnosticsService service(fake.toDeps());

    ProbeRequest request;
    request.game_id = "CS2";
    request.duration_seconds = 1;
    const CancellationToken cancellation;
    const auto result = service.run(request, cancellation);
    REQUIRE(result.ok());
    CHECK(result.value.outcome == DiagnosticOutcome::LocalNetworkProblem);
    CHECK(result.value.confidence == ConfidenceLevel::High);
}

TEST_CASE("DiagnosticsService: маршрут до игры плох, эталон хорош — улучшение вероятно") {
    FakeDeps fake;
    fake.endpoints = {"93.184.216.34:27015"};
    fake.direct_samples = badLatencySamples();
    fake.gateway_samples = goodSamples();
    fake.probe_samples = {12.0}; // эталон отвечает быстро
    DiagnosticsService service(fake.toDeps());

    ProbeRequest request;
    request.game_id = "CS2";
    request.duration_seconds = 1;
    const CancellationToken cancellation;
    const auto result = service.run(request, cancellation);
    REQUIRE(result.ok());
    CHECK(result.value.outcome == DiagnosticOutcome::ImprovementLikely);
    CHECK(result.value.confidence == ConfidenceLevel::Medium);
    REQUIRE(result.value.candidates.size() == 1);
}

TEST_CASE("DiagnosticsService: плохо везде — проблема канала") {
    FakeDeps fake;
    fake.endpoints = {"93.184.216.34:27015"};
    fake.direct_samples = badLatencySamples();
    fake.gateway_samples = goodSamples();
    fake.probe_samples = {150.0}; // эталон тоже плох
    DiagnosticsService service(fake.toDeps());

    ProbeRequest request;
    request.game_id = "CS2";
    request.duration_seconds = 1;
    const CancellationToken cancellation;
    const auto result = service.run(request, cancellation);
    REQUIRE(result.ok());
    CHECK(result.value.outcome == DiagnosticOutcome::LocalNetworkProblem);
    CHECK(result.value.confidence == ConfidenceLevel::Medium);
}

TEST_CASE("DiagnosticsService: эталон недоступен — недостаточно данных для сравнения") {
    FakeDeps fake;
    fake.endpoints = {"93.184.216.34:27015"};
    fake.direct_samples = badLatencySamples();
    fake.gateway_samples = goodSamples();
    fake.probe_available = false;
    DiagnosticsService service(fake.toDeps());

    ProbeRequest request;
    request.game_id = "CS2";
    request.duration_seconds = 1;
    const CancellationToken cancellation;
    const auto result = service.run(request, cancellation);
    REQUIRE(result.ok());
    CHECK(result.value.outcome == DiagnosticOutcome::InsufficientData);
    CHECK(result.value.confidence == ConfidenceLevel::Low);
}

TEST_CASE("DiagnosticsService: слишком мало ответов от сервера") {
    FakeDeps fake;
    fake.endpoints = {"93.184.216.34:27015"};
    fake.direct_samples = {30.0, 31.0}; // только 2 ответа
    fake.gateway_samples = goodSamples();
    DiagnosticsService service(fake.toDeps());

    ProbeRequest request;
    request.game_id = "CS2";
    request.duration_seconds = 1;
    const CancellationToken cancellation;
    const auto result = service.run(request, cancellation);
    REQUIRE(result.ok());
    CHECK(result.value.outcome == DiagnosticOutcome::InsufficientData);
}

// ---------------------------------------------------------------------------
// MultipathEngine
// ---------------------------------------------------------------------------

namespace {
// Запускает движок на несколько циклов зондирования и возвращает метрики
// последнего цикла. Движок работает в отдельном потоке, поэтому без ожидания
// по condition_variable тесты были бы недетерминированы.
std::vector<PathMetrics> runEngineForCycles(MultipathEngine& engine,
                                            std::initializer_list<std::string> gateways,
                                            int cycles, int timeout_ms = 3000) {
    std::mutex m;
    std::condition_variable cv;
    std::vector<PathMetrics> last;
    int seen = 0;

    engine.setMetricsUpdateCallback([&](const std::vector<PathMetrics>& metrics) {
        std::lock_guard<std::mutex> lk(m);
        last = metrics;
        if (++seen >= cycles) cv.notify_one();
    });

    engine.start("8.8.8.8");
    for (const auto& g : gateways) engine.addRoute(g);

    std::unique_lock<std::mutex> lk(m);
    cv.wait_for(lk, std::chrono::milliseconds(timeout_ms));
    return last;
}

size_t distinctLatencies(const std::vector<PathMetrics>& ms) {
    size_t distinct = 0;
    for (size_t i = 0; i < ms.size(); ++i) {
        bool unique = true;
        for (size_t j = 0; j < ms.size(); ++j) {
            if (i != j && ms[j].latency_ms == ms[i].latency_ms) { unique = false; break; }
        }
        if (unique) ++distinct;
    }
    return distinct;
}

// Латентность равна последнему октету шлюза — детерминирована и уникальна
// для каждого шлюза, поэтому видно, что пути зондируются по-разному.
MultipathEngine::ProbeSample lastOctetProbe(const std::string& target) {
    const size_t dot = target.rfind('.');
    double lat = 1.0;
    if (dot != std::string::npos && dot + 1 < target.size()) {
        lat = static_cast<double>(std::stoi(target.substr(dot + 1)));
    }
    return {lat, 0.0};
}
} // namespace

TEST_CASE("MultipathEngine: каждый путь зондируется по своему шлюзу") {
    MultipathConfig cfg;
    cfg.probe_interval_ms = 30;
    cfg.auto_switch = false;
    MultipathEngine engine(cfg);
    engine.setPathProbe(lastOctetProbe);

    auto metrics = runEngineForCycles(engine, {"10.0.0.41", "10.0.0.42"}, 3);
    engine.stop();

    REQUIRE(metrics.size() >= 2);
    // Раньше все пути зондировались до одного и того же game_server_ip_ и
    // получали бы одинаковую латентность.
    CHECK(distinctLatencies(metrics) == metrics.size());

    bool has41 = false, has42 = false;
    for (const auto& m : metrics) {
        if (m.latency_ms == 41.0) has41 = true;
        if (m.latency_ms == 42.0) has42 = true;
    }
    CHECK(has41);
    CHECK(has42);
}

TEST_CASE("MultipathEngine: путь без ответов помечается нездоровым") {
    MultipathConfig cfg;
    cfg.probe_interval_ms = 30;
    cfg.auto_switch = false;
    MultipathEngine engine(cfg);
    engine.setPathProbe([](const std::string& t) {
        if (t == "10.0.0.9") return MultipathEngine::ProbeSample{15.0, 0.0};
        return MultipathEngine::ProbeSample{0.0, 100.0}; // не отвечает
    });

    auto metrics = runEngineForCycles(engine, {"10.0.0.9", "10.0.0.10"}, 3);
    engine.stop();

    REQUIRE_FALSE(metrics.empty());
    for (const auto& m : metrics) {
        if (m.latency_ms == 15.0) {
            CHECK(m.is_healthy);
        } else {
            CHECK_FALSE(m.is_healthy);
        }
    }

    const auto best = engine.getBestPath();
    REQUIRE(best);
    CHECK(best->latency_ms == 15.0);
}

TEST_CASE("MultipathEngine: джиттер сглаживается, а не перезаписывается скачком") {
    MultipathConfig cfg;
    cfg.probe_interval_ms = 20;
    cfg.auto_switch = false;
    MultipathEngine engine(cfg);
    engine.setPathProbe([](const std::string& t) {
        static std::map<std::string, int> calls;
        const int c = ++calls[t];
        // Один выброс на 110 мс, дальше стабильно 10 мс.
        return MultipathEngine::ProbeSample{(c == 2) ? 110.0 : 10.0, 0.0};
    });

    auto metrics = runEngineForCycles(engine, {"10.0.0.55"}, 9);
    engine.stop();

    bool smoothed = false;
    for (const auto& m : metrics) {
        if (m.jitter_ms > 5.0) smoothed = true;
    }
    // При перезаписи одним |delta| после выброса джиттер обнулился бы.
    CHECK(smoothed);
}

TEST_CASE("MultipathEngine: автоматическое переключение на лучший путь") {
    MultipathConfig cfg;
    cfg.probe_interval_ms = 30;
    cfg.auto_switch = true;
    MultipathEngine engine(cfg);
    engine.setPathProbe([](const std::string& t) {
        if (t == "10.0.0.9") return MultipathEngine::ProbeSample{15.0, 0.0};
        return MultipathEngine::ProbeSample{150.0, 10.0}; // плохой путь
    });

    std::atomic<uint32_t> switched_to{0};
    engine.setPathSwitchCallback([&](uint32_t, uint32_t to) { switched_to = to; });

    runEngineForCycles(engine, {"10.0.0.1", "10.0.0.9"}, 4);
    engine.stop();

    CHECK(switched_to.load() != 0);
    CHECK(engine.getActivePathId() == switched_to.load());
    CHECK(engine.getActivePathId() != 0);

    const auto best = engine.getBestPath();
    REQUIRE(best);
    CHECK(best->latency_ms == 15.0);
}

TEST_CASE("MultipathEngine: дубликаты шлюза и удаление путей") {
    MultipathEngine engine;
    engine.addRoute("10.0.0.7");
    engine.addRoute("10.0.0.7"); // тот же шлюз — не второй путь
    CHECK(engine.getPathMetrics().size() == 1);

    engine.removeRoute("10.0.0.7");
    CHECK(engine.getPathMetrics().empty());
    CHECK_FALSE(engine.getBestPath().has_value());
}

TEST_CASE("MultipathEngine: stop() срабатывает быстро при длинном интервале") {
    MultipathConfig cfg;
    cfg.probe_interval_ms = 10000;
    cfg.auto_switch = false;
    MultipathEngine engine(cfg);
    engine.setPathProbe([](const std::string&) { return MultipathEngine::ProbeSample{1.0, 0.0}; });

    engine.start("8.8.8.8");
    const auto t0 = std::chrono::steady_clock::now();
    engine.stop();
    const auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
    // Прерываемый сон: раньше stop() мог ждать целый probe_interval (10 с).
    CHECK(dt < 1000);

    engine.stop(); // повторный вызов безопасен
    CHECK_FALSE(engine.isActive());
}

// ---------------------------------------------------------------------------
// Регрессии monitoring
// ---------------------------------------------------------------------------

TEST_CASE("StatsCollector: поканальные счётчики дают корректные средние") {
    StatsCollector sc;
    sc.start("s");

    // Раньше recordJitter/recordPacketLoss не увеличивали total_samples,
    // поэтому средние считались по неверному знаменателю.
    sc.recordPing(40.0);
    sc.recordPing(60.0);
    sc.recordJitter(2.0);
    sc.recordJitter(6.0);
    sc.recordPacketLoss(1.0);
    sc.recordPacketLoss(3.0);
    sc.stop();

    const auto session = sc.getCurrentSession();
    REQUIRE(session.total_samples == 6);
    REQUIRE(session.total_ping_ms == 100.0);
    REQUIRE(session.total_jitter_ms == 8.0);
    REQUIRE(session.total_packet_loss_percent == 4.0);
}

TEST_CASE("StatsCollector: повторный stop() не дублирует сессию") {
    StatsCollector sc;
    sc.stop();                     // stop() без start() — ничего не добавляется
    sc.start("s");
    sc.stop();
    sc.stop();                     // двойной stop() — один дубль, не два
    REQUIRE(sc.getPastSessions().size() == 1);
}

TEST_CASE("StatsCollector: loadSession пропускает шапку и мусорные строки") {
    const std::string path = "stats_collector_regression.csv";
    {
        std::ofstream out(path);
        out << "Session: regression\n";
        out << "Samples: 2\n";
        out << "Average Ping: 50 ms\n";
        out << "\n";
        out << "Snapshots:\n";
        out << "10,1.0,0.5\n";
        out << "20,2.0,1.5\n";
        out << "garbage line,not a number\n";
        out << "30,3.0\n"; // неполная строка
    }

    StatsCollector sc;
    REQUIRE(sc.loadSession(path));
    const auto session = sc.getCurrentSession();
    REQUIRE(session.snapshots.size() == 2);
    REQUIRE(session.snapshots[0].ping_ms == 10.0);
    REQUIRE(session.snapshots[1].jitter_ms == 2.0);
    REQUIRE(session.total_samples == 2);

    std::remove(path.c_str());
}

TEST_CASE("PacketLossMonitor: start() учитывает интервал и число пакетов") {
    // Раньше monitorLoop() хардкодил 50 пакетов и 5 с, параметры start()
    // игнорировались. Проверяем, что они сохранились и применяются.
    PacketLossMonitor plm;
    plm.start("8.8.8.8", 500, 5);
    REQUIRE(plm.isRunning());
    plm.stop();
    REQUIRE_FALSE(plm.isRunning());

    const auto stats = plm.getStats();
    // avg_loss_percent теперь реально вычисляется, а не остаётся 0 в stats_.
    CHECK(stats.avg_loss_percent >= 0.0);
}

TEST_CASE("PingMonitor: unanswered echo классифицируется как Timeout") {
    // 240.0.0.1 — резервный диапазон, маршрута до него нет: IcmpSendEcho
    // вернёт 0. Раньше error оставался InternalFailure — теперь Timeout.
    PingMonitor monitor;
    const auto result = monitor.ping("240.0.0.1", 500);
    REQUIRE_FALSE(result.success);
    CHECK(result.error == DiagnosticError::Timeout);
}


// ===================== remediation layer: bug-audit regressions =====================

TEST_CASE("FixTransaction::record() отдаёт снимок, а не ссылку на внутреннее состояние") {
    using namespace gno::remediation;
    // Раньше record() возвращал const TransactionRecord&, отпуская мьютекс в
    // момент возврата. Вызывающий (часто другой поток — UI, опрашивающий
    // статус) получал ссылку на record_ без защиты, а persist(record()) в
    // execute() держал эту ссылку всё время записи файла в backup store.
    FakeStateApi api;
    MemoryBackupStore store;
    std::vector<std::unique_ptr<FixAction>> actions;
    actions.push_back(std::make_unique<WindowsFixAction>(ActionId::PowerPlan, api));
    std::vector<ActionTarget> targets{ActionTarget{NoTarget{}}};
    FixTransaction tx("tx-snapshot", std::move(actions), std::move(targets), store);

    REQUIRE(tx.prepare().ok());
    const TransactionRecord before = tx.record();   // снимок через публичный API
    REQUIRE(before.status == TransactionStatus::Prepared);

    REQUIRE(tx.execute().ok());
    REQUIRE(tx.record().status == TransactionStatus::Applied);

    // Снимок, взятый раньше, не должен «видеть» более поздние изменения: иначе
    // параллельный читатель наблюдает разорванное состояние без блокировки.
    CHECK(before.status == TransactionStatus::Prepared);
}

TEST_CASE("JsonBackupStore: повреждённый файл в журнале не роняет процесс") {
    using namespace gno::remediation;
    // Чужой или сломанный json в папке Backups раньше вызывал необработанное
    // исключение (std::stod/std::stoul) прямо при чтении журнала — приложение
    // закрывалось при попытке показать историю.
    const auto dir = std::filesystem::temp_directory_path() / "gno_test_backup_corrupt";
    std::error_code rm_ec;
    std::filesystem::remove_all(dir, rm_ec);
    JsonBackupStore store(dir.string());

    {
        std::ofstream f(dir / "bad_number.json");
        f << "{\"transaction_id\":\"tx-bad-num\",\"n\":1e999}";
        std::ofstream g(dir / "bad_escape.json");
        g << "{\"transaction_id\":\"tx-bad-esc\",\"s\":\"\\uZZZZ\"}";
        std::ofstream h(dir / "lone_e.json");
        h << "{\"transaction_id\":\"tx-lone-e\",\"n\":e}";
        std::ofstream i(dir / "garbage.json");
        i << "not a json at all";
        std::ofstream j(dir / "empty.json");
        j << "";
    }

    auto listed = store.list();
    REQUIRE(listed.ok());
    CHECK(listed.value().empty());

    CHECK_FALSE(store.load("tx-bad-num").ok());
    CHECK_FALSE(store.load("tx-bad-esc").ok());
    CHECK_FALSE(store.load("no-such-transaction").ok());

    std::filesystem::remove_all(dir, rm_ec);
}

TEST_CASE("JsonBackupStore: creation_time цели процесса не теряет точность") {
    using namespace gno::remediation;
    // Реальное значение FILETIME для запущенного процесса ~1.3e17 тиков — далеко
    // за 2^53. В JSON-числе младшие биты терялись бы, и после перечитывания
    // записи метка процесса уже не совпадала бы с настоящей (а по ней движок
    // теперь проверяет, что pid не переиспользован).
    const auto dir = std::filesystem::temp_directory_path() / "gno_test_backup_proc";
    std::error_code rm_ec;
    std::filesystem::remove_all(dir, rm_ec);
    JsonBackupStore store(dir.string());

    TransactionRecord rec;
    rec.transaction_id = "tx-proc";
    rec.status = TransactionStatus::Applied;
    rec.prepared_actions.push_back(PreparedAction{
        ActionId::ProcessPriority,
        ActionTarget{ProcessTarget{1234, 0x01D8AABBCCDDEEFFULL, "C://Games//game.exe"}},
        ActionState{}, ActionState{}, true});
    rec.applied_action_order.push_back(ActionId::ProcessPriority);

    REQUIRE(store.save(rec));
    auto loaded = store.load("tx-proc");
    REQUIRE(loaded.ok());
    const auto restored = std::get<ProcessTarget>(loaded.value().prepared_actions[0].target);
    const auto original = std::get<ProcessTarget>(rec.prepared_actions[0].target);
    CHECK(restored.pid == 1234);
    CHECK(restored.path == "C://Games//game.exe");
    CHECK(restored.creation_time == 0x01D8AABBCCDDEEFFULL);
    CHECK(restored == original);

    std::filesystem::remove_all(dir, rm_ec);
}

#ifdef _WIN32
TEST_CASE("WindowsStateApi: приоритет не применяется к процессу с чужим creation_time") {
    using namespace gno::remediation;
    // PID может быть переиспользован ОС сразу после выхода игры. Цель несёт
    // creation_time именно для проверки: без неё откат мог поменять приоритет
    // чужому процессу, успевшему получить тот же pid.
    WindowsStateApi api;
    const std::uint32_t self_pid = static_cast<std::uint32_t>(GetCurrentProcessId());
    FILETIME created{}, exited{}, kernel{}, user{};
    REQUIRE(GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user));
    const std::uint64_t real_creation =
        (static_cast<std::uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;

    const ProcessTarget live{self_pid, real_creation, ""};
    const ProcessTarget stale{self_pid, real_creation + 1, ""};

    auto baseline = api.getPriority(live);
    REQUIRE(baseline.ok());

    // Тот же pid, но другая метка: значит, это уже не тот процесс.
    CHECK_FALSE(api.getPriority(stale).ok());
    CHECK(api.getPriority(stale).code() == RemediationError::InvalidTarget);
    CHECK_FALSE(api.setPriority(stale, PriorityLevel::High).ok());
    CHECK(api.setPriority(stale, PriorityLevel::High).code() == RemediationError::InvalidTarget);

    // Живая цель меняется и возвращается обратно — проверка не сломала запись.
    REQUIRE(api.setPriority(live, PriorityLevel::AboveNormal));
    CHECK(api.getPriority(live).value() == PriorityLevel::AboveNormal);
    REQUIRE(api.setPriority(live, baseline.value()));
    CHECK(api.getPriority(live).value() == baseline.value());
}

TEST_CASE("WindowsStateApi: netsh на несуществующем интерфейсе не падает") {
    using namespace gno::remediation;
    // После переписывания runCommand (job object + поток чтения + потолок в 15 с)
    // путь должен остаться рабочим: процесс создаётся, вывод читается, дерево
    // и хэндлы убираются. Несуществующий индекс гарантирует, что мы ничего не
    // меняем в системе, а netsh завершается ошибкой, а не зависанием.
    WindowsStateApi api;
    const InterfaceTarget bogus{"{00000000-0000-0000-0000-000000000000}", 4294967295ull};
    auto mtu = api.setMtu(bogus, MtuValue{1500});
    CHECK_FALSE(mtu.ok());
    CHECK(mtu.code() != RemediationError::None);
}
#endif

// ---------------------------------------------------------------------------
// Вариант B: мост UI — русский пересказ DiagnosticReport (plain_language) и
// шлюз пути для уведомлений о переключении (MultipathEngine).
// ---------------------------------------------------------------------------

TEST_CASE("PlainLanguageReport: исходы диагностики получают русские названия") {
    using namespace gno;
    CHECK(PlainLanguageReport::outcomeLabel(DiagnosticOutcome::ImprovementLikely) == "Улучшение вероятно");
    CHECK(PlainLanguageReport::outcomeLabel(DiagnosticOutcome::NoImprovementFound) == "Улучшать нечего");
    CHECK(PlainLanguageReport::outcomeLabel(DiagnosticOutcome::LocalNetworkProblem) == "Проблема в локальной сети");
    CHECK(PlainLanguageReport::outcomeLabel(DiagnosticOutcome::InsufficientData) == "Недостаточно данных");

    CHECK(PlainLanguageReport::confidenceLabel(ConfidenceLevel::High) == "высокая");
    CHECK(PlainLanguageReport::confidenceLabel(ConfidenceLevel::Medium) == "средняя");
    CHECK(PlainLanguageReport::confidenceLabel(ConfidenceLevel::Low) == "низкая");

    // Каждое значение ошибки должно иметь перевод — иначе пользователь
    // увидел бы сырой идентификатор перечисления.
    for (int e = 0; e <= static_cast<int>(DiagnosticError::InternalFailure); ++e) {
        const auto label = PlainLanguageReport::errorLabel(static_cast<DiagnosticError>(e));
        CHECK_FALSE(label.empty());
    }
}

TEST_CASE("PlainLanguageReport: рекомендации по диагнозу маршрута") {
    using namespace gno;

    DiagnosticReport report;
    report.direct = {10, 10, 30.0, 45.0, 2.0, 0.0};
    report.gateway = {10, 10, 5.0, 8.0, 0.0, 0.0};
    report.candidates.push_back({"EU", {10, 10, 12.0, 15.0, 1.0, 0.0}, {}});

    // Маршрут в порядке — рекомендация объясняет, что дело не в сети.
    report.outcome = DiagnosticOutcome::NoImprovementFound;
    report.confidence = ConfidenceLevel::High;
    std::string ok = PlainLanguageReport::diagnosticRecommendation(report);
    CHECK(ok.find("30") != std::string::npos);
    CHECK(ok.find("не виноваты") != std::string::npos);

    // Проблема в локальной сети — совет про роутер/кабель на метриках шлюза.
    report.outcome = DiagnosticOutcome::LocalNetworkProblem;
    report.gateway = {10, 10, 150.0, 160.0, 0.0, 7.0};
    std::string local = PlainLanguageReport::diagnosticRecommendation(report);
    CHECK(local.find("роутер") != std::string::npos);
    CHECK(local.find("150") != std::string::npos);

    // Улучшение вероятно — упоминаем ограничение продукта (VPN не строим).
    report.gateway = {10, 10, 5.0, 8.0, 0.0, 0.0};
    report.direct = {10, 10, 150.0, 160.0, 0.0, 0.0};
    report.outcome = DiagnosticOutcome::ImprovementLikely;
    std::string improve = PlainLanguageReport::diagnosticRecommendation(report);
    CHECK(improve.find("VPN") != std::string::npos);

    // Недостаточно данных — рекомендация пустая: MonitoringService не должен
    // пулять в статус-бар совет, который нечем подкрепить.
    report.outcome = DiagnosticOutcome::InsufficientData;
    CHECK(PlainLanguageReport::diagnosticRecommendation(report).empty());
}

TEST_CASE("PlainLanguageReport: отчёт содержит исход, уверенность и конечную точку") {
    using namespace gno;

    DiagnosticReport report;
    report.outcome = DiagnosticOutcome::LocalNetworkProblem;
    report.confidence = ConfidenceLevel::High;
    report.endpoint.ip = Ipv4Address::parse("93.184.216.34").value_or(Ipv4Address{});
    report.endpoint.port = 27015;
    report.endpoint.protocol = TransportProtocol::Udp;
    report.evidence = {"Шлюз: задержка 150 мс (p95 160), джиттер 7.0 мс, потери 7.0% (3/10)",
                       "Вторая строка доказательств"};

    std::vector<std::string> evidence;
    const std::string text = PlainLanguageReport::formatDiagnosticReport(report, &evidence);

    CHECK(text.find("Проблема в локальной сети") != std::string::npos);
    CHECK(text.find("высокая") != std::string::npos);
    CHECK(text.find("93.184.216.34:27015") != std::string::npos);
    CHECK(text.find("UDP") != std::string::npos);
    REQUIRE(evidence.size() == 2);
    CHECK(evidence[1] == "Вторая строка доказательств");
}

TEST_CASE("MultipathEngine::getPathGateway: адрес шлюза по id пути") {
    using namespace gno;
    MultipathConfig cfg;
    cfg.probe_interval_ms = 30;
    cfg.auto_switch = false;
    MultipathEngine engine(cfg);

    // Пути появляются через addRoute — именно так их создаёт start() для
    // каждого интерфейса со своим шлюзом.
    engine.addRoute("192.168.1.1");
    engine.addRoute("10.0.0.1");

    REQUIRE(engine.getPathGateway(0).has_value());
    CHECK(*engine.getPathGateway(0) == "192.168.1.1");
    REQUIRE(engine.getPathGateway(1).has_value());
    CHECK(*engine.getPathGateway(1) == "10.0.0.1");

    // Несуществующий путь — nullopt, а не пустая строка: интерфейс отличает
    // «нет такого пути» от «путь без шлюза».
    CHECK_FALSE(engine.getPathGateway(999).has_value());

    // Удалённый путь перестаёт выдавать шлюз.
    engine.removeRoute("192.168.1.1");
    CHECK_FALSE(engine.getPathGateway(0).has_value());
}

// --- Вариант E, item 4: ICMP-зонд инъектируется, бенчмарки тестыруются без сети.

TEST_CASE("SpeedTest: бенчмарк сервера через инъектированный зонд") {
    using namespace gno;
    SpeedTest test;
    int calls = 0;
    test.setPingProbe([&calls](const std::string& ip, int /*timeout_ms*/) {
        ++calls;
        return ip == "1.1.1.1" ? 12.0 : 40.0;
    });

    // 5 зондов усредняются — раньше это означало 5 настоящих IcmpSendEcho,
    // которые в sandbox отваливались по таймауту и давали success=false
    // ни из-за чего.
    const PingResult r = test.benchmarkServer("1.1.1.1", 5);
    CHECK(r.success);
    CHECK(r.latency_ms == doctest::Approx(12.0));
    CHECK(r.server_ip == "1.1.1.1");
    CHECK(calls == 5);

    // probes ограничен сверху 10 — защита от вечного цикла на бракованном вводе.
    const PingResult big = test.benchmarkServer("8.8.8.8", 999);
    CHECK(big.latency_ms == doctest::Approx(40.0));
}

TEST_CASE("SpeedTest: таймаут зонда — это провал, а не нулевая задержка") {
    using namespace gno;
    SpeedTest test;
    test.setPingProbe([](const std::string&, int) { return -1.0; });

    const PingResult r = test.benchmarkServer("240.0.0.1", 3);
    CHECK_FALSE(r.success);
    CHECK(r.latency_ms == 0.0);

    // Граничный случай: ровно 0.0 — это валидный «мгновенный» ответ (localhost),
    // а не таймаут. Поэтому условие успеха — ms >= 0.0, а не > 0.0.
    test.setPingProbe([](const std::string&, int) { return 0.0; });
    const PingResult local = test.benchmarkServer("127.0.0.1", 2);
    CHECK(local.success);
    CHECK(local.latency_ms == 0.0);
}

TEST_CASE("SpeedTest: runBenchmark по конкретной цели в отдельном потоке") {
    using namespace gno;
    SpeedTest test;
    test.setPingProbe([](const std::string& ip, int) {
        return ip == "1.1.1.1" ? 7.0 : -1.0;
    });

    test.runBenchmark("1.1.1.1");
    test.stop();   // join — иначе воркер бы прожил дольше теста

    const auto results = test.getResults();
    REQUIRE(results.size() == 1);
    CHECK(results[0].server_ip == "1.1.1.1");
    CHECK(results[0].success);
    CHECK(results[0].latency_ms == doctest::Approx(7.0));
    CHECK_FALSE(test.isRunning());

    // Недостижимая цель: результат пишется, но с success=false.
    // Новый прогон runBenchmark очищает прежние результаты — поэтому здесь
    // ровно одна запись, а не две.
    test.runBenchmark("240.0.0.1");
    test.stop();
    const auto failed = test.getResults();
    REQUIRE(failed.size() == 1);
    CHECK_FALSE(failed[0].success);
    CHECK(failed[0].server_ip == "240.0.0.1");
}

TEST_CASE("DNSManager: бенчмарк и выбор быстрейшего через инъектированный зонд") {
    using namespace gno;
    DNSManager mgr;
    std::map<std::string, double> fake = {
        {"1.1.1.1", 5.0}, {"1.1.1.2", 30.0}, {"8.8.8.8", 20.0},
        {"9.9.9.9", 15.0}, {"208.67.222.222", 25.0}, {"77.88.8.8", 40.0},
    };
    mgr.setPingProbe([&fake](const std::string& ip, int) {
        const auto it = fake.find(ip);
        return it == fake.end() ? -1.0 : it->second;
    });

    // По 3 зонда на сервер: усреднение должно давать ровно заданное значение.
    const DNSBenchmarkResult single = mgr.benchmarkServer("9.9.9.9");
    CHECK(single.success);
    CHECK(single.latency_ms == doctest::Approx(15.0));

    const auto all = mgr.benchmarkAll();
    REQUIRE(all.size() == mgr.getPresets().size());
    CHECK(std::all_of(all.begin(), all.end(),
                      [](const DNSBenchmarkResult& r) { return r.success; }));

    // Быстрейший — Cloudflare 1.1.1.1, а не «первый попавшийся».
    const DNSBenchmarkResult best = mgr.getFastestServer();
    REQUIRE(best.success);
    CHECK(best.server == "1.1.1.1");
    CHECK(best.latency_ms == doctest::Approx(5.0));
}

TEST_CASE("DNSManager: недостижимый сервер помечается провалом, а не нулём") {
    using namespace gno;
    DNSManager mgr;
    mgr.setPingProbe([](const std::string& ip, int) {
        // Только один пресет отвечает; остальные «молчат».
        return ip == "8.8.8.8" ? 20.0 : -1.0;
    });

    const auto all = mgr.benchmarkAll();
    REQUIRE_FALSE(all.empty());
    const auto ok = std::count_if(all.begin(), all.end(),
                                  [](const DNSBenchmarkResult& r) { return r.success; });
    // Google и Google(Family) оба шлют 8.8.8.8 как primary — отвечают оба.
    CHECK(ok == 2);

    const DNSBenchmarkResult best = mgr.getFastestServer();
    REQUIRE(best.success);
    CHECK(best.server == "8.8.8.8");
}

TEST_CASE("defaultIcmpPing: невалидный адрес даёт -1.0 без обращения к сети") {
    using namespace gno;
    const IcmpPing probe = defaultIcmpPing();
    REQUIRE(probe);

    // Бессмысленный адрес не должен уйти в IcmpSendEcho: раньше при сбое
    // inet_pton в addr оставался мусор. Теперь — отказ с -1.0.
    const double ms = probe("definitely.not.an.ip", 500);
    CHECK(ms < 0.0);
}

// --- Вариант C, item 2: единый лог сессии.

namespace {
// Свой каталог на каждый тест, чтобы они не мешали друг другу и не трогали
// настоящий %APPDATA%\GNO.
std::filesystem::path freshLogDir(const std::string& name) {
    const auto dir = std::filesystem::temp_directory_path() / ("gno-logger-" + name);
    std::filesystem::remove_all(dir);
    return dir;
}
} // namespace

TEST_CASE("logger: пишет строки в файл сессии и отдаёт его путь") {
    using namespace gno;
    const auto dir = freshLogDir("basic");

    log::init("GNO-test", dir.string());
    const std::string path = log::currentLogPath();
    REQUIRE_FALSE(path.empty());
    CHECK(std::filesystem::exists(path));

    log::write(log::Level::Info, "первая строка");
    log::write(log::Level::Warning, "вторая строка");
    log::shutdown();

    // Поток чтения закрываем до remove_all: на Windows открытый файл удалить
    // нельзя, и уборка каталога падала бы с filesystem error.
    {
        std::ifstream in(path, std::ios::binary);
        REQUIRE(in.is_open());
        const std::string content((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
        CHECK(content.find("первая строка") != std::string::npos);
        CHECK(content.find("вторая строка") != std::string::npos);
        CHECK(content.find("[INFO]") != std::string::npos);
        CHECK(content.find("[WARN]") != std::string::npos);
    }

    std::filesystem::remove_all(dir);
}

TEST_CASE("logger: запись до init — безопасный no-op") {
    using namespace gno;
    log::shutdown();
    CHECK(log::currentLogPath().empty());

    // Главное — не упасть: без открытого файла строка просто теряется.
    log::write(log::Level::Error, "в никуда");
    CHECK(log::currentLogPath().empty());
}

TEST_CASE("logger: ротация оставляет только свежие файлы и не трогает чужие") {
    using namespace gno;
    const auto dir = freshLogDir("rotate");
    std::filesystem::create_directories(dir);

    for (int day = 1; day <= 5; ++day) {
        char name[32];
        std::snprintf(name, sizeof(name), "gno-2020-01-%02d.log", day);
        std::ofstream(dir / name) << "x";
    }
    // Посторонние файлы не должны ни считаться логами, ни удаляться.
    std::ofstream(dir / "gno-notes.txt") << "x";
    std::ofstream(dir / "other.log") << "x";

    log::rotate(dir.string(), 2);

    int kept = 0;
    for (const auto& e : std::filesystem::directory_iterator(dir)) {
        const std::string n = e.path().filename().string();
        if (n.size() == 18 && n.rfind("gno-", 0) == 0 && n.substr(14) == ".log") ++kept;
    }
    CHECK(kept == 2);
    CHECK(std::filesystem::exists(dir / "gno-2020-01-05.log"));
    CHECK(std::filesystem::exists(dir / "gno-2020-01-04.log"));
    CHECK_FALSE(std::filesystem::exists(dir / "gno-2020-01-01.log"));
    CHECK(std::filesystem::exists(dir / "gno-notes.txt"));
    CHECK(std::filesystem::exists(dir / "other.log"));

    std::filesystem::remove_all(dir);
}

TEST_CASE("logger: конкурентная запись не рвёт строки") {
    using namespace gno;
    const auto dir = freshLogDir("threads");

    log::init("GNO-test", dir.string());
    const std::string path = log::currentLogPath();
    REQUIRE_FALSE(path.empty());

    constexpr int kThreads = 4;
    constexpr int kPerThread = 50;
    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([t]() {
            for (int i = 0; i < kPerThread; ++i)
                log::write(log::Level::Debug,
                           "t" + std::to_string(t) + "-" + std::to_string(i));
        });
    }
    for (auto& w : workers) w.join();
    log::shutdown();

    int intact = 0;
    {
        std::ifstream in(path, std::ios::binary);
        std::string line;
        while (std::getline(in, line)) {
            // Целая строка = есть и тег уровня, и начало полезной нагрузки.
            // Если бы мьютекса не было, строки перемешались бы и эта проверка
            // поймала бы обрубки.
            if (line.find("[DEBUG] t") != std::string::npos) ++intact;
        }
    }
    CHECK(intact == kThreads * kPerThread);

    std::filesystem::remove_all(dir);
}

TEST_CASE("logger: без переменных окружения лог не падает в рабочий каталог") {
    using namespace gno;
#ifdef _WIN32
    // Песочница, служба или урезанное окружение могут не передать %APPDATA%.
    // Раньше фолбэком был текущий рабочий каталог, и рядом с приложением
    // появлялась папка GNO — в том числе там, где писать нельзя. Теперь
    // фолбэк — временный каталог.
    const char* savedEnv = std::getenv("APPDATA");
    const std::string saved = savedEnv ? savedEnv : "";

    _putenv_s("APPDATA", "");
    log::init("GNO-fallback-test");
    const std::string path = log::currentLogPath();
    log::shutdown();
    _putenv_s("APPDATA", saved.c_str());

    REQUIRE_FALSE(path.empty());
    const std::string cwd = std::filesystem::current_path().string();
    CHECK(std::filesystem::path(path).string().rfind(cwd, 0) != 0);

    // Убираем за собой временный каталог.
    std::error_code ec;
    std::filesystem::remove_all(
        std::filesystem::temp_directory_path(ec) / "GNO-fallback-test", ec);
#endif
}

// --- Вариант D, item 2: текстовые отчёты (Markdown/HTML).

namespace {
gno::DiagnosticReport sampleDiagnosticReport() {
    using namespace gno;
    DiagnosticReport rep;
    rep.outcome = DiagnosticOutcome::ImprovementLikely;
    rep.confidence = ConfidenceLevel::High;
    rep.game_id = "cs2";
    rep.endpoint.ip = *Ipv4Address::parse("1.2.3.4");
    rep.endpoint.port = 27015;
    rep.endpoint.protocol = TransportProtocol::Udp;

    rep.gateway.sent = 30;
    rep.gateway.received = 30;
    rep.gateway.median_ms = 12.5;
    rep.gateway.p95_ms = 20.0;
    rep.gateway.jitter_ms = 1.5;
    rep.gateway.loss_percent = 0.0;

    rep.direct.sent = 30;
    rep.direct.received = 27;
    rep.direct.median_ms = 42.25;
    rep.direct.p95_ms = 60.0;
    rep.direct.jitter_ms = 4.5;
    rep.direct.loss_percent = 10.0;

    rep.evidence = {"шлюз отвечает стабильно", "потери на пути к серверу игры"};
    return rep;
}
} // namespace

TEST_CASE("report_text: Markdown-отчёт диагностики содержит исход, метрики и доказательства") {
    using namespace gno;
    const DiagnosticReport rep = sampleDiagnosticReport();
    report::ReportHeader header;
    header.app_version = "3.1.0";
    header.generated_at = "2026-09-20 19:00";

    const std::string md = report::diagnosticReportToMarkdown(rep, header);

    CHECK(md.find("# Диагностика маршрута") != std::string::npos);
    CHECK(md.find("3.1.0") != std::string::npos);
    CHECK(md.find("2026-09-20 19:00") != std::string::npos);
    CHECK(md.find("Улучшение вероятно") != std::string::npos);
    CHECK(md.find("высокая") != std::string::npos);
    CHECK(md.find("1.2.3.4:27015 (UDP)") != std::string::npos);
    CHECK(md.find("cs2") != std::string::npos);
    // Метрики обеих веток попали в таблицу.
    CHECK(md.find("12.50") != std::string::npos);
    CHECK(md.find("42.25") != std::string::npos);
    CHECK(md.find("10.00") != std::string::npos);
    // Доказательства — отдельными пунктами списка.
    CHECK(md.find("- шлюз отвечает стабильно") != std::string::npos);
    CHECK(md.find("- потери на пути к серверу игры") != std::string::npos);
}

TEST_CASE("report_text: HTML-отчёт экранирует пользовательские строки") {
    using namespace gno;
    DiagnosticReport rep = sampleDiagnosticReport();
    // Имя игры и evidence приходят из внешнего мира — в HTML их вставлять
    // сырыми нельзя.
    rep.game_id = "a&b<c>d\"e";
    rep.evidence = {"<script>alert(1)</script>"};

    const std::string html = report::diagnosticReportToHtml(rep);

    CHECK(html.find("<script>") == std::string::npos);
    CHECK(html.find("&lt;script&gt;") != std::string::npos);
    CHECK(html.find("a&amp;b&lt;c&gt;d&quot;e") != std::string::npos);
    // Разметка при этом настоящая, а не экранированная целиком.
    CHECK(html.find("<h1>Диагностика маршрута") != std::string::npos);
}

TEST_CASE("report_text: пустые доказательства дают явную пометку, а не пустой раздел") {
    using namespace gno;
    DiagnosticReport rep = sampleDiagnosticReport();
    rep.evidence.clear();

    CHECK(report::diagnosticReportToMarkdown(rep).find("- нет данных") != std::string::npos);
    CHECK(report::diagnosticReportToHtml(rep).find("нет данных") != std::string::npos);
}

TEST_CASE("report_text: отчёт без игры и без конечной точки не ломается") {
    using namespace gno;
    DiagnosticReport rep;   // всё по умолчанию: InsufficientData, 0.0.0.0
    rep.outcome = DiagnosticOutcome::InsufficientData;

    const std::string md = report::diagnosticReportToMarkdown(rep);
    const std::string html = report::diagnosticReportToHtml(rep);

    CHECK(md.find("Недостаточно данных") != std::string::npos);
    CHECK(md.find("не определена") != std::string::npos);
    // Без рекомендации (InsufficientData) — заголовок «Рекомендация» не появляется.
    CHECK(md.find("Игра:") == std::string::npos);
    CHECK(html.find("не определена") != std::string::npos);
}

TEST_CASE("report_text: история сессий выгружается в Markdown и HTML") {
    using namespace gno;
    std::vector<SessionRecord> records;
    SessionRecord r;
    r.game_name = "Dota | 2";
    r.start_time_str = "2026-09-20 18:00";
    r.end_time_str = "2026-09-20 18:45";
    r.duration_seconds = 2700;
    r.avg_ping_ms = 33.5;
    r.avg_jitter_ms = 2.25;
    r.avg_packet_loss = 0.5;
    r.quality_score = 88.0;
    r.boost_was_active = true;
    records.push_back(r);

    const std::string md = report::sessionHistoryToMarkdown(records, 33.5, 2.25, 0.5);
    CHECK(md.find("# История сессий") != std::string::npos);
    CHECK(md.find("33.50") != std::string::npos);
    // «|» в имени игры экранируется, иначе строка таблицы распалась бы.
    CHECK(md.find("Dota \\| 2") != std::string::npos);
    CHECK(md.find("| да |") != std::string::npos);

    const std::string html = report::sessionHistoryToHtml(records, 33.5, 2.25, 0.5);
    CHECK(html.find("<table>") != std::string::npos);
    CHECK(html.find("88.00") != std::string::npos);

    // Пустая история — не пустой файл, а явная пометка.
    const std::string emptyMd = report::sessionHistoryToMarkdown({}, 0, 0, 0);
    CHECK(emptyMd.find("Сессий:** 0") != std::string::npos);
    CHECK(report::sessionHistoryToHtml({}, 0, 0, 0).find("нет записей") != std::string::npos);
}

TEST_CASE("report_text: пустая шапка подставляет текущее время") {
    using namespace gno;
    const std::string md = report::diagnosticReportToMarkdown(sampleDiagnosticReport());
    // Формат «YYYY-MM-DD HH:MM» — проверяем наличие разделителя даты и времени.
    const auto pos = md.find("**Сформировано:** ");
    REQUIRE(pos != std::string::npos);
    const std::string stamp = md.substr(pos, 40);
    CHECK(stamp.find('-') != std::string::npos);
    CHECK(stamp.find(':') != std::string::npos);
}

// --- Вариант D, item 4: подбор статей базы знаний под свежий диагноз.

TEST_CASE("ProblemDb::forDiagnostic: InsufficientData не даёт советов") {
    using namespace gno;
    DiagnosticReport rep;
    rep.outcome = DiagnosticOutcome::InsufficientData;
    rep.game_id = "cs2";

    // Без данных любой подбор был бы гаданием — лучше пусто, чем случайная статья.
    CHECK(ProblemDb::forDiagnostic(rep).empty());
}

TEST_CASE("ProblemDb::forDiagnostic: статьи релевантны исходу и метрикам") {
    using namespace gno;
    DiagnosticReport rep;
    rep.outcome = DiagnosticOutcome::LocalNetworkProblem;
    rep.gateway.median_ms = 120.0;
    rep.gateway.loss_percent = 9.0;
    rep.gateway.jitter_ms = 30.0;

    const auto hits = ProblemDb::forDiagnostic(rep);
    REQUIRE_FALSE(hits.empty());
    CHECK(hits.size() <= 5);

    // Каждая выданная статья обязана содержать хотя бы одно из ключевых слов
    // исхода/метрик — иначе это был бы мусор в рекомендациях.
    const std::vector<std::string> expected = {
        "роутер", "локальн", "wifi", "wi-fi", "шлюз", "кабел", "потер", "сеть",
        "задержк", "нестабильн",
    };
    for (const auto& e : hits) {
        const std::string text = e.title + " " + e.symptoms + " " + e.cause + " " + e.solution;
        std::string low = text;
        std::transform(low.begin(), low.end(), low.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        bool matched = false;
        for (const auto& kw : expected) {
            if (low.find(kw) != std::string::npos) { matched = true; break; }
        }
        CHECK(matched);
    }
}

TEST_CASE("ProblemDb::forDiagnostic: своя игра получает приоритет") {
    using namespace gno;
    DiagnosticReport rep;
    rep.outcome = DiagnosticOutcome::ImprovementLikely;
    rep.game_id = "cs2";
    rep.direct.median_ms = 95.0;

    const auto hits = ProblemDb::forDiagnostic(rep);
    REQUIRE_FALSE(hits.empty());

    // Если среди подобранного есть статьи про CS2, они должны идти раньше
    // остальных: у них решение конкретнее.
    std::size_t firstForeign = hits.size();
    for (std::size_t i = 0; i < hits.size(); ++i) {
        if (hits[i].game != "CS2") { firstForeign = i; break; }
    }
    for (std::size_t i = firstForeign; i < hits.size(); ++i)
        CHECK(hits[i].game != "CS2");
}

TEST_CASE("ProblemDb::forDiagnostic: смена исхода меняет подборку") {
    using namespace gno;
    DiagnosticReport net;
    net.outcome = DiagnosticOutcome::LocalNetworkProblem;

    DiagnosticReport perf;
    perf.outcome = DiagnosticOutcome::NoImprovementFound;

    const auto netHits = ProblemDb::forDiagnostic(net);
    const auto perfHits = ProblemDb::forDiagnostic(perf);

    // Разные исходы — разные ключевые слова, поэтому подборки не совпадают
    // дословно (иначе связь «диагноз → статьи» была бы фиктивной).
    bool differs = netHits.size() != perfHits.size();
    if (!differs) {
        for (std::size_t i = 0; i < netHits.size(); ++i) {
            if (netHits[i].title != perfHits[i].title) { differs = true; break; }
        }
    }
    CHECK(differs);
}

// ===========================================================================
// SHA-256 (core/sha256.h)
// ===========================================================================

TEST_CASE("sha256: известные векторы") {
    using gno::sha256::hashString;
    CHECK(hashString("") ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(hashString("abc") ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(hashString("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("sha256: миллион символов 'a' (проверка длины > одного блока)") {
    gno::sha256::Hasher h;
    const std::string chunk(1000, 'a');
    for (int i = 0; i < 1000; ++i)
        h.update(chunk.data(), chunk.size());
    CHECK(h.final() ==
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("sha256: нарезка на куски не меняет результат") {
    // Дробление обязано совпадать с хешем цельной строки — иначе потоковое
    // хеширование скачанного файла даст ложное несовпадение.
    const std::string text =
        "Обновление E2E4 Soft: проверка целостности дистрибутива по SHA-256.";
    const std::string whole = gno::sha256::hashString(text);
    // Эталон hashlib: 110 байт, три блока с паддингом.
    CHECK(whole == "470dbfe978ac701ab73558fd2dd60ee1bf57223261ada7839c3c6e727977b2ef");

    for (std::size_t step : {std::size_t(1), std::size_t(3), std::size_t(7),
                             std::size_t(63), std::size_t(64), std::size_t(65),
                             std::size_t(200)}) {
        gno::sha256::Hasher h;
        for (std::size_t i = 0; i < text.size(); i += step)
            h.update(text.data() + i, std::min(step, text.size() - i));
        CHECK(h.final() == whole);
    }
}

TEST_CASE("sha256: границы блоков 55/56/64 байта") {
    // 55/56/64 — точки, где паддинг требует второго блока. Сверяем с
    // эталоном hashlib (Python): независимая реализация, а не наш же код.
    CHECK(gno::sha256::hashString(std::string(55, 'x')) ==
          "d5e285683cd4efc02d021a5c62014694958901005d6f71e89e0989fac77e4072");
    CHECK(gno::sha256::hashString(std::string(56, 'x')) ==
          "04c26261370ee7541549d16dee320c723e3fd14671e66a099afe0a377c16888e");
    CHECK(gno::sha256::hashString(std::string(56, '\0')) ==
          "d4817aa5497628e7c77e6b606107042bbba3130888c5f47a375e6179be789fbb");
}

TEST_CASE("sha256: повторный final не портит первый результат") {
    gno::sha256::Hasher h;
    h.update("abc", 3);
    const std::string first = h.final();
    CHECK(first ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    // Второй вызов возвращает пустую строку, а не мусор.
    CHECK(h.final().empty());
}

TEST_CASE("sha256: hashFile читает файл целиком") {
    const auto dir = std::filesystem::temp_directory_path() / "gno-sha-test";
    std::filesystem::create_directories(dir);
    const auto path = dir / "sample.bin";

    const std::string content(200000, 'Z'); // > одного буфера чтения
    {
        std::ofstream out(path, std::ios::binary);
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    std::string hex;
    std::string error;
    CHECK(gno::sha256::hashFile(path.string(), hex, error));
    CHECK(hex == gno::sha256::hashString(content));
    // Эталон hashlib для 200000 байт 'Z' — проверяет и потоковое чтение,
    // и корректность длины в паддинге (> 2^16 бит).
    CHECK(hex == "38b2f7cf459e18e9f488b5164cd553ba6c0dc286df97c0121e319ca601730362");

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("sha256: hashFile на отсутствующем файле отдаёт ошибку") {
    std::string hex;
    std::string error;
    CHECK_FALSE(gno::sha256::hashFile("нет-такого-файла-12345.bin", hex, error));
    CHECK_FALSE(error.empty());
}

// ===========================================================================
// JSON-парсер (core/simple_json.h)
// ===========================================================================

TEST_CASE("json: разбор объекта и массива") {
    gno::json::Value v;
    std::string error;
    REQUIRE(gno::json::parse(R"({"a":1,"b":[true,false,null,"s"]})", v, error));
    CHECK(v.isObject());
    CHECK(v["a"].asNumber() == doctest::Approx(1.0));
    CHECK(v["b"].isArray());
    CHECK(v["b"].size() == 4);
    CHECK(v["b"].at(0).asBool() == true);
    CHECK(v["b"].at(1).asBool() == false);
    CHECK(v["b"].at(2).isNull());
    CHECK(v["b"].at(3).asString() == "s");
    // Отсутствующее поле и выход за границы дают Null, а не падение.
    CHECK(v["нет"].isNull());
    CHECK(v["b"].at(99).isNull());
}

TEST_CASE("json: escape-последовательности и \\u") {
    gno::json::Value v;
    std::string error;
    REQUIRE(gno::json::parse(R"({"s":"a\"b\\c\/d\ne\tf"})", v, error));
    CHECK(v["s"].asString() == "a\"b\\c/d\ne\tf");

    // \u0041 → 'A', \u041f\u0440\u0438\u0432\u0435\u0442 → "Привет"
    REQUIRE(gno::json::parse(R"({"s":"\u0041\u041f\u0440\u0438\u0432\u0435\u0442"})",
                             v, error));
    CHECK(v["s"].asString() == "AПривет");
}

TEST_CASE("json: суррогатная пара превращается в один символ UTF-8") {
    gno::json::Value v;
    std::string error;
    // U+1F600 (😀) = \ud83d\ude00 → F0 9F 98 80
    REQUIRE(gno::json::parse(R"({"e":"\ud83d\ude00"})", v, error));
    const std::string& s = v["e"].asString();
    REQUIRE(s.size() == 4);
    CHECK(static_cast<unsigned char>(s[0]) == 0xF0);
    CHECK(static_cast<unsigned char>(s[1]) == 0x9F);
    CHECK(static_cast<unsigned char>(s[2]) == 0x98);
    CHECK(static_cast<unsigned char>(s[3]) == 0x80);
}

TEST_CASE("json: одиночная младшая суррогатная пара — ошибка") {
    gno::json::Value v;
    std::string error;
    CHECK_FALSE(gno::json::parse(R"({"e":"\ude00"})", v, error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("json: числа, в т.ч. дробные и с экспонентой") {
    gno::json::Value v;
    std::string error;
    REQUIRE(gno::json::parse(R"([0,-12,3.5,1e3,2.5e-2])", v, error));
    CHECK(v.at(0).asNumber() == doctest::Approx(0.0));
    CHECK(v.at(1).asNumber() == doctest::Approx(-12.0));
    CHECK(v.at(2).asNumber() == doctest::Approx(3.5));
    CHECK(v.at(3).asNumber() == doctest::Approx(1000.0));
    CHECK(v.at(4).asNumber() == doctest::Approx(0.025));
}

TEST_CASE("json: битые документы отвергаются с описанием") {
    gno::json::Value v;
    std::string error;

    CHECK_FALSE(gno::json::parse("", v, error));
    CHECK_FALSE(error.empty());

    CHECK_FALSE(gno::json::parse(R"({"a":})", v, error));
    CHECK_FALSE(gno::json::parse(R"({"a":1}x)", v, error));      // мусор после
    CHECK_FALSE(gno::json::parse(R"({"a":1)", v, error));        // не закрыт
    CHECK_FALSE(gno::json::parse(R"({"a" 1})", v, error));       // нет ':'
    CHECK_FALSE(gno::json::parse(R"([1,])", v, error));          // висячая запятая
    CHECK_FALSE(gno::json::parse(R"("abc)", v, error));          // незакрытая строка
    CHECK_FALSE(gno::json::parse(R"({"a":01x})", v, error));     // мусор в числе
}

TEST_CASE("json: переполнение double не бросает исключение") {
    gno::json::Value v;
    std::string error;
    // Раньше std::stod вылетал бы исключением прямо в разборе сетевого ответа.
    const bool parsed = gno::json::parse(R"({"n":1e999})", v, error);
    CHECK_FALSE(parsed);
    CHECK_FALSE(error.empty());
}

TEST_CASE("json: ограничение глубины вложенности") {
    gno::json::Value v;
    std::string error;
    std::string deep(200, '[');
    CHECK_FALSE(gno::json::parse(deep, v, error));

    // Умеренная вложенность разбирается нормально.
    std::string ok = "[{\"a\":[[1]]}]";
    CHECK(gno::json::parse(ok, v, error));
}

TEST_CASE("json: BOM в начале ответа не ломает разбор") {
    gno::json::Value v;
    std::string error;
    const std::string with_bom = "\xEF\xBB\xBF{\"a\":1}";
    REQUIRE(gno::json::parse(with_bom, v, error));
    CHECK(v["a"].asNumber() == doctest::Approx(1.0));
}

// ===========================================================================
// Апдейтер (core/updater.h)
// ===========================================================================

namespace {

// Реалистичный ответ GitHub Releases API с digest (формат 2024+).
const char* const kReleaseJson = R"({
  "tag_name": "v3.2.0",
  "name": "E2E4 Soft 3.2.0",
  "html_url": "https://github.com/Reagent420/e2e4-soft/releases/tag/v3.2.0",
  "body": "Что нового:\n- проверка SHA-256\n- автообновление",
  "assets": [
    {
      "name": "GNO-windows-x64.zip",
      "browser_download_url": "https://example.invalid/GNO-windows-x64.zip",
      "digest": "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
    },
    {
      "name": "GNO-setup.exe",
      "browser_download_url": "https://example.invalid/GNO-setup.exe",
      "digest": "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    }
  ]
})";

} // namespace

TEST_CASE("updater: сравнение версий") {
    using gno::updater::isNewerVersion;
    CHECK(isNewerVersion("3.1.1", "3.1.0"));
    CHECK(isNewerVersion("v3.1.1", "3.1.0"));
    CHECK(isNewerVersion("4.0.0", "3.99.99"));
    CHECK(isNewerVersion("3.2", "3.1.9"));
    CHECK(isNewerVersion("3.2.0", "3.1.9"));

    CHECK_FALSE(isNewerVersion("3.1.0", "3.1.0"));
    CHECK_FALSE(isNewerVersion("3.0.9", "3.1.0"));
    CHECK_FALSE(isNewerVersion("v3.1.0", "3.1.0"));
    // Предрелиз младше той же версии без суффикса.
    CHECK_FALSE(isNewerVersion("3.2.0-beta1", "3.2.0"));
    CHECK(isNewerVersion("3.2.0", "3.2.0-beta1"));
    CHECK(isNewerVersion("3.2.0-beta1", "3.1.9"));
    // Мусорный тег не считается обновлением.
    CHECK_FALSE(isNewerVersion("nightly", "3.1.0"));
    CHECK_FALSE(isNewerVersion("", "3.1.0"));
}

TEST_CASE("updater: normalizeDigest") {
    using gno::updater::normalizeDigest;
    std::string hex;
    CHECK(normalizeDigest(std::string("sha256:") + std::string(64, 'A'), hex));
    CHECK(hex == std::string(64, 'a'));
    CHECK(normalizeDigest(std::string(64, 'b'), hex));
    CHECK(hex == std::string(64, 'b'));

    CHECK_FALSE(normalizeDigest("", hex));
    CHECK_FALSE(normalizeDigest("sha256:", hex));
    CHECK_FALSE(normalizeDigest(std::string(63, 'a'), hex));    // короткий
    CHECK_FALSE(normalizeDigest(std::string(65, 'a'), hex));    // длинный
    CHECK_FALSE(normalizeDigest(std::string(64, 'z'), hex));    // не hex
    CHECK_FALSE(normalizeDigest("sha256-abc", hex));            // формат SRI
}

TEST_CASE("updater: parseLatestRelease видит обновление") {
    const auto r = gno::updater::parseLatestRelease(kReleaseJson, "3.1.0");
    REQUIRE(r.ok);
    CHECK(r.error.empty());
    CHECK(r.update_available);
    CHECK(r.latest_version == "v3.2.0");
    CHECK(r.release.tag == "v3.2.0");
    CHECK(r.release.name == "E2E4 Soft 3.2.0");
    CHECK(r.release.notes.find("SHA-256") != std::string::npos);
    REQUIRE(r.release.assets.size() == 2);
    CHECK(r.release.assets[1].name == "GNO-setup.exe");
    CHECK(r.release.assets[1].sha256 == std::string(64, 'a'));
    CHECK(r.release.assets[0].sha256 ==
          "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
}

TEST_CASE("updater: parseLatestRelease при актуальной версии") {
    const auto r = gno::updater::parseLatestRelease(kReleaseJson, "3.2.0");
    REQUIRE(r.ok);
    CHECK_FALSE(r.update_available);
    CHECK(r.current_version == "3.2.0");
}

TEST_CASE("updater: parseLatestRelease отвергает битый ответ") {
    auto r = gno::updater::parseLatestRelease("не json", "3.1.0");
    CHECK_FALSE(r.ok);
    CHECK_FALSE(r.error.empty());

    r = gno::updater::parseLatestRelease("{}", "3.1.0");
    CHECK_FALSE(r.ok); // нет tag_name

    r = gno::updater::parseLatestRelease("[]", "3.1.0");
    CHECK_FALSE(r.ok); // не объект
}

TEST_CASE("updater: pickWindowsAsset выбирает установщик, а не zip") {
    const auto r = gno::updater::parseLatestRelease(kReleaseJson, "3.1.0");
    const auto* asset = gno::updater::pickWindowsAsset(r.release);
    REQUIRE(asset != nullptr);
    CHECK(asset->name == "GNO-setup.exe");
}

TEST_CASE("updater: pickWindowsAsset возвращает nullptr, если exe нет") {
    gno::updater::Release release;
    release.assets.push_back({"GNO-linux.tar.gz", "u", ""});
    release.assets.push_back({"checksums.txt", "u", ""});
    CHECK(gno::updater::pickWindowsAsset(release) == nullptr);
}

TEST_CASE("updater: pickWindowsAsset умеет брать zip") {
    // Реальные релизы проекта выкладывают именно zip — без этого запасного
    // варианта обновление было бы недоступно.
    gno::updater::Release release;
    release.assets.push_back({"checksums.txt", "u", ""});
    release.assets.push_back({"GNO_v2.8.0.zip", "u", ""});
    const auto* asset = gno::updater::pickWindowsAsset(release);
    REQUIRE(asset != nullptr);
    CHECK(asset->name == "GNO_v2.8.0.zip");

    // exe всё равно в приоритете над zip.
    gno::updater::Release both;
    both.assets.push_back({"GNO_v3.zip", "u", ""});
    both.assets.push_back({"GNO-setup.exe", "u", ""});
    const auto* preferred = gno::updater::pickWindowsAsset(both);
    REQUIRE(preferred != nullptr);
    CHECK(preferred->name == "GNO-setup.exe");
}

TEST_CASE("updater: разбор реального ответа GitHub (структура релиза проекта)") {
    // Слепок настоящего ответа /repos/Reagent420/e2e4-soft/releases/latest
    // (тег v3.0.1, единственный ассет — zip с digest). Проверяет, что парсер
    // понимает живую структуру, а не только синтетическую.
    const char* const real = R"({
      "url": "https://api.github.com/repos/Reagent420/e2e4-soft/releases/380621502",
      "html_url": "https://github.com/Reagent420/e2e4-soft/releases/tag/v3.0.1",
      "id": 380621502,
      "tag_name": "v3.0.1",
      "name": "v3.0.1 — Reboot Dialog + Audit",
      "draft": false,
      "prerelease": false,
      "body": "Что нового:\n- диалог перезагрузки\n- аудит",
      "assets": [
        {
          "url": "https://api.github.com/repos/Reagent420/e2e4-soft/releases/assets/1",
          "id": 1,
          "name": "GNO_v2.8.0.zip",
          "content_type": "application/zip",
          "size": 12345678,
          "download_count": 3,
          "browser_download_url": "https://github.com/Reagent420/e2e4-soft/releases/download/v3.0.1/GNO_v2.8.0.zip",
          "digest": "sha256:aa8c8aabb114f5f6793246676de9ac638d66987cc49baf8fc643b0c2673d9790"
        }
      ]
    })";

    const auto r = gno::updater::parseLatestRelease(real, "3.0.0");
    REQUIRE(r.ok);
    CHECK(r.update_available);
    CHECK(r.latest_version == "v3.0.1");
    REQUIRE(r.release.assets.size() == 1);
    CHECK(r.release.assets[0].sha256 ==
          "aa8c8aabb114f5f6793246676de9ac638d66987cc49baf8fc643b0c2673d9790");
    CHECK(r.release.assets[0].download_url.find("GNO_v2.8.0.zip") != std::string::npos);

    const auto* asset = gno::updater::pickWindowsAsset(r.release);
    REQUIRE(asset != nullptr);
    CHECK(asset->name == "GNO_v2.8.0.zip");

    // Наша текущая версия 3.1.0 новее v3.0.1 — обновление не предлагается.
    const auto current = gno::updater::parseLatestRelease(real, "3.1.0");
    REQUIRE(current.ok);
    CHECK_FALSE(current.update_available);
}

TEST_CASE("updater: sidecar-дайджест") {
    gno::updater::Release release;
    release.assets.push_back({"GNO-setup.exe", "u1", ""});
    release.assets.push_back({"GNO-setup.exe.sha256", "u2", ""});

    const auto* sidecar = gno::updater::findSidecarAsset(release, release.assets[0]);
    REQUIRE(sidecar != nullptr);
    CHECK(sidecar->name == "GNO-setup.exe.sha256");

    std::string hex;
    CHECK(gno::updater::parseSidecarDigest(std::string(64, 'c') + "  GNO-setup.exe", hex));
    CHECK(hex == std::string(64, 'c'));
    CHECK(gno::updater::parseSidecarDigest(std::string(64, 'D') + "\n", hex));
    CHECK(hex == std::string(64, 'd'));
    CHECK_FALSE(gno::updater::parseSidecarDigest("мусор", hex));

    // Нет sidecar — nullptr, а не случайный ассет.
    release.assets.pop_back();
    CHECK(gno::updater::findSidecarAsset(release, release.assets[0]) == nullptr);
}

TEST_CASE("updater: check с инъектированным HTTP") {
    gno::updater::Updater engine;
    CHECK_FALSE(engine.hasHttpGet());
    // Без заданного транспорта проверка обязана честно сообщить об ошибке,
    // а не вернуть «обновлений нет».
    auto r = engine.check("3.1.0");
    CHECK_FALSE(r.ok);
    CHECK_FALSE(r.error.empty());

    engine.setHttpGet([](const std::string& url, std::string& body, std::string& error) {
        if (url.find("api.github.com/repos/Reagent420/e2e4-soft/releases/latest") ==
            std::string::npos) {
            error = "неожиданный URL: " + url;
            return false;
        }
        body = kReleaseJson;
        return true;
    });
    CHECK(engine.hasHttpGet());

    r = engine.check("3.1.0");
    REQUIRE(r.ok);
    CHECK(r.update_available);

    // Сетевой сбой пробрасывается наружу.
    engine.setHttpGet([](const std::string&, std::string&, std::string& error) {
        error = "нет соединения";
        return false;
    });
    r = engine.check("3.1.0");
    CHECK_FALSE(r.ok);
    CHECK(r.error == "нет соединения");
}

TEST_CASE("updater: URL репозитория и API") {
    CHECK(gno::updater::repositoryUrl() == "https://github.com/Reagent420/e2e4-soft");
    CHECK(gno::updater::Updater::releasesApiUrl() ==
          "https://api.github.com/repos/Reagent420/e2e4-soft/releases/latest");
}

TEST_CASE("updater: downloadAndVerify принимает файл с верным SHA-256") {
    const auto dir = std::filesystem::temp_directory_path() / "gno-updater-test";
    std::filesystem::create_directories(dir);
    const auto dest = dir / "update.bin";

    const std::string payload = "тело обновления";
    const std::string expected = gno::sha256::hashString(payload);

    gno::updater::Updater engine;
    engine.setHttpDownload([&payload](const std::string&, const std::string& path,
                                      std::string&) {
        std::ofstream out(path, std::ios::binary);
        out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
        return true;
    });
    engine.setFileHasher([](const std::string& path, std::string& hex, std::string& error) {
        return gno::sha256::hashFile(path, hex, error);
    });

    gno::updater::Asset asset{"update.bin", "https://example.invalid/update.bin", expected};
    std::string actual;
    std::string error;
    CHECK(engine.downloadAndVerify(asset, dest.string(), actual, error));
    CHECK(error.empty());
    CHECK(actual == expected);
    CHECK(std::filesystem::exists(dest));

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("updater: downloadAndVerify отвергает несовпавший SHA-256") {
    const auto dir = std::filesystem::temp_directory_path() / "gno-updater-bad";
    std::filesystem::create_directories(dir);
    const auto dest = dir / "update.bin";

    gno::updater::Updater engine;
    engine.setHttpDownload([](const std::string&, const std::string& path, std::string&) {
        std::ofstream out(path, std::ios::binary);
        out << "подменённое содержимое";
        return true;
    });
    engine.setFileHasher([](const std::string& path, std::string& hex, std::string& error) {
        return gno::sha256::hashFile(path, hex, error);
    });

    gno::updater::Asset asset{"update.bin", "https://example.invalid/update.bin",
                              std::string(64, 'f')};
    std::string actual;
    std::string error;
    CHECK_FALSE(engine.downloadAndVerify(asset, dest.string(), actual, error));
    CHECK(error.find("SHA-256") != std::string::npos);
    // Файл с неверным хешем не должен остаться на диске.
    CHECK_FALSE(std::filesystem::exists(dest));

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("updater: downloadAndVerify отказывается без ожидаемого хеша") {
    gno::updater::Updater engine;
    engine.setHttpDownload([](const std::string&, const std::string&, std::string&) {
        return true;
    });
    engine.setFileHasher([](const std::string&, std::string&, std::string&) {
        return true;
    });

    gno::updater::Asset asset{"update.bin", "https://example.invalid/update.bin", ""};
    std::string actual;
    std::string error;
    CHECK_FALSE(engine.downloadAndVerify(asset, "неважно.bin", actual, error));
    CHECK(error.find("SHA-256") != std::string::npos);
}

TEST_CASE("updater: downloadAndVerify сообщает о сбое скачивания") {
    gno::updater::Updater engine;
    engine.setHttpDownload([](const std::string&, const std::string&, std::string& error) {
        error = "обрыв соединения";
        return false;
    });
    engine.setFileHasher([](const std::string&, std::string&, std::string&) {
        return true;
    });

    gno::updater::Asset asset{"u.bin", "https://example.invalid/u.bin", std::string(64, 'a')};
    std::string actual;
    std::string error;
    CHECK_FALSE(engine.downloadAndVerify(asset, "неважно.bin", actual, error));
    CHECK(error == "обрыв соединения");
}
