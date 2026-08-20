#include "doctest.h"
#include "core/game_profiles.h"
#include "core/input_validation.h"
#include "core/json_persistence.h"
#include "core/game_watcher.h"
#include "core/session_history.h"
#include "optimization/fps_optimizer.h"
#include "diagnostics/diagnostic_types.h"
#include "diagnostics/endpoint_observer.h"
#include "diagnostics/network_sampler.h"
#include "diagnostics/probe_client.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace {

class FakeEndpointObserver final : public gno::IEndpointObserver {
public:
    gno::DiagnosticResult<std::vector<gno::ObservedEndpoint>> observe(
        uint32_t, std::chrono::milliseconds, const gno::CancellationToken&) override {
        return {{}, gno::DiagnosticError::None};
    }
};

class FakeNetworkSampler final : public gno::INetworkSampler {
public:
    gno::DiagnosticResult<gno::MetricSummary> sample(
        const gno::SampleTarget&, const gno::SamplePlan&, const gno::CancellationToken&) override {
        return {{}, gno::DiagnosticError::PermissionDenied};
    }
};

class FakeProbeClient final : public gno::IProbeClient {
public:
    gno::DiagnosticResult<gno::ProbeMeasurement> measure(
        const gno::ProbeRequest&, const gno::CancellationToken&) override {
        return {{}, gno::DiagnosticError::Cancelled};
    }
};

static_assert(gno::DiagnosticReport::network_settings_changed == false,
              "diagnostic reports must never change network settings");
static_assert(std::is_same<decltype(gno::DiagnosticReport::network_settings_changed),
                           const bool>::value,
              "network settings invariant must be immutable");
static_assert(std::has_virtual_destructor<gno::IEndpointObserver>::value);
static_assert(std::has_virtual_destructor<gno::INetworkSampler>::value);
static_assert(std::has_virtual_destructor<gno::IProbeClient>::value);

class RestoredGameProfiles {
public:
    RestoredGameProfiles()
        : root_(std::filesystem::temp_directory_path() / "gno-restored-profile-tests"),
          profiles_(std::make_unique<gno::GameProfiles>(root_)) {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
        std::filesystem::create_directories(root_, error);
        profiles_ = std::make_unique<gno::GameProfiles>(root_);
    }

    ~RestoredGameProfiles() {
        profiles_.reset();
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    gno::GameProfiles& get() { return *profiles_; }

private:
    std::filesystem::path root_;
    std::unique_ptr<gno::GameProfiles> profiles_;
};

class TemporaryStorageRoot {
public:
    explicit TemporaryStorageRoot(const std::string& name)
        : path_(std::filesystem::temp_directory_path() / name) {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
        std::filesystem::create_directories(path_);
    }

    ~TemporaryStorageRoot() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

void writeTextFile(const std::filesystem::path& path, const std::string& content) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    REQUIRE(output);
    output << content;
    REQUIRE(output);
}

} // namespace

TEST_CASE("diagnostic defaults are safe") {
    gno::DiagnosticReport report;
    CHECK(report.outcome == gno::DiagnosticOutcome::InsufficientData);
    CHECK(report.confidence == gno::ConfidenceLevel::Low);
    CHECK(report.network_settings_changed == false);
}

TEST_CASE("profile mutation defaults are disabled") {
    const gno::GameProfile profile;
    CHECK_FALSE(profile.multipath_enabled);
    CHECK_FALSE(profile.fps_boost_enabled);
    CHECK_FALSE(profile.network_optimization);
    CHECK_FALSE(profile.auto_apply);
}

TEST_CASE("mutating service defaults require explicit opt in") {
    const gno::GameWatcherConfig watcher;
    CHECK_FALSE(watcher.auto_apply_profiles);

    const gno::FPSBoostConfig optimizer;
    CHECK_FALSE(optimizer.disable_game_dvr);
    CHECK_FALSE(optimizer.disable_fullscreen_optimizations);
    CHECK_FALSE(optimizer.optimize_power_plan);
    CHECK_FALSE(optimizer.set_high_priority);
    CHECK_FALSE(optimizer.disable_mouse_acceleration);
    CHECK_FALSE(optimizer.optimize_virtual_memory);
}

TEST_CASE("IPv4 parser accepts decimal address boundaries") {
    const auto unspecified = gno::Ipv4Address::parse("0.0.0.0");
    REQUIRE(unspecified);
    CHECK(unspecified->isUnspecified());
    CHECK(unspecified->toString() == "0.0.0.0");

    const auto maximum = gno::Ipv4Address::parse("255.255.255.255");
    REQUIRE(maximum);
    CHECK_FALSE(maximum->isUnspecified());
    CHECK(maximum->toString() == "255.255.255.255");
}

TEST_CASE("IPv4 parser rejects malformed non-decimal input") {
    const std::vector<std::string_view> invalid = {
        "", "1.2.3", "1.2.3.4.5", "1..2.3", "1.2.3.256", "-1.2.3.4",
        "+1.2.3.4", " 1.2.3.4", "1.2.3.4 ", "1.2.3.4x", "x1.2.3.4",
        "game.example.com", "https://155.133.226.10"
    };
    for (const auto value : invalid) {
        CHECK_FALSE(gno::Ipv4Address::parse(value));
    }
}

TEST_CASE("IPv4 address formatting round-trips parsed octets") {
    const auto address = gno::Ipv4Address::parse("155.133.226.10");
    REQUIRE(address);
    CHECK(address->toString() == "155.133.226.10");
    CHECK(gno::Ipv4Address::parse(address->toString()) == address);
}

TEST_CASE("probe request contains no hostname or URL") {
    const auto endpoint = gno::Ipv4Address::parse("155.133.226.10");
    REQUIRE(endpoint);
    gno::ProbeRequest request{"counter-strike-2", *endpoint, 27015,
                              gno::TransportProtocol::Udp, 30};
    CHECK(request.game_id == "counter-strike-2");
    CHECK(request.duration_seconds == 30);
    CHECK_FALSE(gno::Ipv4Address::parse("game.example.com"));
    CHECK_FALSE(gno::Ipv4Address::parse("https://155.133.226.10"));
}

TEST_CASE("cancellation token observes cancellation after source destruction") {
    gno::CancellationToken token;
    {
        gno::CancellationSource source;
        token = source.token();
        source.cancel();
        CHECK(token.isCancelled());
    }
    CHECK(token.isCancelled());
}

TEST_CASE("moved cancellation sources are inert and preserve moved-to cancellation") {
    gno::CancellationSource original;
    gno::CancellationSource moved_to(std::move(original));
    const auto token = moved_to.token();

    CHECK_FALSE(token.isCancelled());
    CHECK_FALSE(original.token().isCancelled());
    original.cancel();
    CHECK_FALSE(token.isCancelled());

    moved_to.cancel();
    CHECK(token.isCancelled());
}

TEST_CASE("diagnostic service interfaces admit platform-neutral implementations") {
    FakeEndpointObserver observer;
    FakeNetworkSampler sampler;
    FakeProbeClient probe;
    const gno::CancellationToken cancellation;

    const auto observed = observer.observe(42, std::chrono::milliseconds(10), cancellation);
    CHECK(observed.ok());
    CHECK(observed.error == gno::DiagnosticError::None);
    CHECK(observed.value.empty());

    const auto sampled = sampler.sample({}, {}, cancellation);
    CHECK_FALSE(sampled.ok());
    CHECK(sampled.error == gno::DiagnosticError::PermissionDenied);
    CHECK(sampled.value.sent == 0);

    const auto measured = probe.measure({}, cancellation);
    CHECK_FALSE(measured.ok());
    CHECK(measured.error == gno::DiagnosticError::Cancelled);
    CHECK(measured.value.probe_region.empty());
}

TEST_CASE("diagnostic results default to a non-success error") {
    gno::DiagnosticResult<gno::MetricSummary> result;
    CHECK_FALSE(result.ok());
    CHECK(result.error == gno::DiagnosticError::InternalFailure);
}

TEST_CASE("bounded integer validation") {
    CHECK(gno::parseBoundedInt("1", 1, 100) == 1);
    CHECK(gno::parseBoundedInt("100", 1, 100) == 100);
    CHECK_FALSE(gno::parseBoundedInt("0", 1, 100));
    CHECK_FALSE(gno::parseBoundedInt("-1", 1, 100));
    CHECK_FALSE(gno::parseBoundedInt("101", 1, 100));
    CHECK_FALSE(gno::parseBoundedInt("10x", 1, 100));
    CHECK_FALSE(gno::parseBoundedInt("", 1, 100));
    CHECK_FALSE(gno::parseBoundedInt("1", 100, 1));
}

TEST_CASE("bounded file reader rejects oversized input") {
    const std::string path = "foundation-oversized.tmp";
    { std::ofstream out(path, std::ios::binary); out << std::string(65, 'x'); }
    CHECK_FALSE(gno::readBoundedFile(path, 64));
    std::remove(path.c_str());
}

TEST_CASE("bounded file reader accepts empty and exact-limit input") {
    const std::string empty_path = "foundation-empty.tmp";
    const std::string exact_path = "foundation-exact.tmp";
    { std::ofstream out(empty_path, std::ios::binary); }
    { std::ofstream out(exact_path, std::ios::binary); out << std::string(64, 'x'); }

    const auto empty = gno::readBoundedFile(empty_path, 64);
    REQUIRE(empty);
    CHECK(empty->empty());

    const auto exact = gno::readBoundedFile(exact_path, 64);
    REQUIRE(exact);
    CHECK(*exact == std::string(64, 'x'));

    std::remove(empty_path.c_str());
    std::remove(exact_path.c_str());
}

TEST_CASE("profile import rejects malformed JSON") {
    const std::string path = "foundation-malformed.json";
    { std::ofstream out(path); out << "{not-json"; }
    RestoredGameProfiles guarded;
    CHECK_FALSE(guarded.get().importFromFile(path));
    std::remove(path.c_str());
}

TEST_CASE("profile import rejects user supplied routes") {
    const std::string path = "foundation-custom-routes.json";
    { std::ofstream out(path); out << R"([{"game_name":"unsafe","process_name":"game.exe","custom_routes":["203.0.113.1"]}])"; }
    RestoredGameProfiles guarded;
    CHECK_FALSE(guarded.get().importFromFile(path));
    std::remove(path.c_str());
}

TEST_CASE("profile cap rejects a 257th profile but permits replacement") {
    const std::string path = "foundation-profile-cap.json";
    {
        std::ofstream out(path);
        out << "[";
        for (int i = 0; i < 256; ++i) {
            if (i != 0) out << ",";
            out << R"({"game_name":"profile-)" << i
                << R"(","process_name":"game.exe"})";
        }
        out << "]";
    }

    RestoredGameProfiles guarded;
    auto& profiles = guarded.get();
    REQUIRE(profiles.importFromFile(path));
    REQUIRE(profiles.getAll().size() == 256);

    gno::GameProfile overflow;
    overflow.game_name = "profile-overflow";
    overflow.process_name = "overflow.exe";
    CHECK_FALSE(profiles.set(overflow));
    CHECK(profiles.getAll().size() == 256);

    gno::GameProfile replacement;
    replacement.game_name = "profile-0";
    replacement.process_name = "replacement.exe";
    CHECK(profiles.set(replacement));
    CHECK(profiles.getAll().size() == 256);
    CHECK(profiles.get("profile-0").process_name == "replacement.exe");

    std::remove(path.c_str());
}

TEST_CASE("profile export to a simple filename creates a regular file") {
    const std::string path = "foundation-profile-export.json";
    RestoredGameProfiles guarded;
    auto& profiles = guarded.get();
    REQUIRE(profiles.exportToFile(path));
    CHECK(std::filesystem::is_regular_file(path));
    std::remove(path.c_str());
}

TEST_CASE("session history preserves every record field through JSON") {
    const std::string input = "foundation-history-input.json";
    const std::string output = "foundation-history-output.json";
    { std::ofstream file(input); file << R"([{"game":"test","start":"2026-08-20 12:00:00","end":"2026-08-20 12:01:00","avg_ping":12.5,"avg_jitter":1.5,"avg_loss":0.25,"max_ping":18.0,"duration":60,"boost":true}])"; }

    gno::SessionHistory source;
    REQUIRE(source.loadFromFile(input));
    REQUIRE(source.saveToFile(output));

    gno::SessionHistory restored;
    REQUIRE(restored.loadFromFile(output));
    const auto records = restored.getAll();
    REQUIRE(records.size() == 1);
    const auto& record = records.front();
    CHECK(record.game_name == "test");
    CHECK(record.start_time_str == "2026-08-20 12:00:00");
    CHECK(record.end_time_str == "2026-08-20 12:01:00");
    CHECK(record.avg_ping_ms == 12.5);
    CHECK(record.avg_jitter_ms == 1.5);
    CHECK(record.avg_packet_loss == 0.25);
    CHECK(record.max_ping_ms == 18.0);
    CHECK(record.duration_seconds == 60);
    CHECK(record.boost_was_active);

    std::remove(input.c_str());
    std::remove(output.c_str());
}

TEST_CASE("profile persistence rejects invalid documents without replacing valid state") {
    TemporaryStorageRoot storage("gno-profile-persistence-regression");
    gno::GameProfiles profiles(storage.path());
    const auto expected_path = storage.path() / "GNO" / "profiles.json";
    CHECK(std::filesystem::path(profiles.getSavePath()) == expected_path);

    gno::GameProfile valid;
    valid.game_name = "stable";
    valid.process_name = "stable.exe";
    REQUIRE(profiles.set(valid));

    writeTextFile(expected_path, "{not-json");
    CHECK_FALSE(profiles.load());
    CHECK(profiles.has("stable"));

    writeTextFile(expected_path, R"({"version":2,"profiles":[]})");
    CHECK_FALSE(profiles.load());
    CHECK(profiles.has("stable"));

    writeTextFile(expected_path, std::string(1024 * 1024 + 1, 'x'));
    CHECK_FALSE(profiles.load());
    CHECK(profiles.has("stable"));
}

TEST_CASE("profile persistence rolls back memory and preserves prior file when temp write fails") {
    TemporaryStorageRoot storage("gno-profile-write-rollback");
    std::size_t writes = 0;
    gno::GameProfiles profiles(
        storage.path(),
        [&](const std::filesystem::path& path, const std::string& content) {
            if (++writes == 2) return false;
            return gno::persistence::atomicWriteText(path, content);
        });
    gno::GameProfile stable;
    stable.game_name = "stable";
    stable.process_name = "stable.exe";
    REQUIRE(profiles.set(stable));

    const auto path = std::filesystem::path(profiles.getSavePath());
    const auto before = *gno::readBoundedFile(path.string(), 1024 * 1024);
    gno::GameProfile rejected;
    rejected.game_name = "rejected";
    rejected.process_name = "rejected.exe";
    CHECK_FALSE(profiles.set(rejected));
    CHECK(profiles.has("stable"));
    CHECK_FALSE(profiles.has("rejected"));
    CHECK(*gno::readBoundedFile(path.string(), 1024 * 1024) == before);
}

TEST_CASE("session persistence rejects invalid documents and negative history counts") {
    TemporaryStorageRoot storage("gno-history-persistence-regression");
    gno::SessionHistory history(storage.path());
    const auto expected_path = storage.path() / "GNO" / "history.json";
    CHECK(std::filesystem::path(history.getSavePath()) == expected_path);

    history.recordStart("stable", false);
    history.recordEnd(10.0, 1.0, 0.0, 12.0);
    REQUIRE(history.getAll().size() == 1);
    CHECK(history.getLast(-1).empty());

    writeTextFile(expected_path, "{not-json");
    CHECK_FALSE(history.loadFromFile());
    CHECK(history.getAll().size() == 1);

    writeTextFile(expected_path, R"({"version":2,"records":[]})");
    CHECK_FALSE(history.loadFromFile());
    CHECK(history.getAll().size() == 1);

    writeTextFile(expected_path, std::string(4 * 1024 * 1024 + 1, 'x'));
    CHECK_FALSE(history.loadFromFile());
    CHECK(history.getAll().size() == 1);
}

TEST_CASE("session persistence leaves state and prior file intact when temp write fails") {
    TemporaryStorageRoot storage("gno-history-write-rollback");
    std::size_t writes = 0;
    gno::SessionHistory history(
        storage.path(),
        [&](const std::filesystem::path& path, const std::string& content) {
            if (++writes == 2) return false;
            return gno::persistence::atomicWriteText(path, content);
        });
    history.recordStart("stable", false);
    history.recordEnd(10.0, 1.0, 0.0, 12.0);
    const auto path = std::filesystem::path(history.getSavePath());
    const auto before = *gno::readBoundedFile(path.string(), 4 * 1024 * 1024);
    history.recordStart("rejected", false);
    history.recordEnd(20.0, 2.0, 0.0, 22.0);
    CHECK(history.getAll().size() == 1);
    CHECK(*gno::readBoundedFile(path.string(), 4 * 1024 * 1024) == before);
}
