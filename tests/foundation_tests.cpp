#include "doctest.h"
#include "core/game_profiles.h"
#include "core/input_validation.h"
#include "core/session_history.h"

#include <cstdio>
#include <fstream>

TEST_CASE("bounded integer validation") {
    CHECK(gno::parseBoundedInt("1", 1, 100) == 1);
    CHECK(gno::parseBoundedInt("100", 1, 100) == 100);
    CHECK_FALSE(gno::parseBoundedInt("0", 1, 100));
    CHECK_FALSE(gno::parseBoundedInt("-1", 1, 100));
    CHECK_FALSE(gno::parseBoundedInt("101", 1, 100));
    CHECK_FALSE(gno::parseBoundedInt("10x", 1, 100));
}

TEST_CASE("bounded file reader rejects oversized input") {
    const std::string path = "foundation-oversized.tmp";
    { std::ofstream out(path, std::ios::binary); out << std::string(65, 'x'); }
    CHECK_FALSE(gno::readBoundedFile(path, 64));
    std::remove(path.c_str());
}

TEST_CASE("profile import rejects malformed JSON") {
    const std::string path = "foundation-malformed.json";
    { std::ofstream out(path); out << "{not-json"; }
    gno::GameProfiles profiles;
    CHECK_FALSE(profiles.importFromFile(path));
    std::remove(path.c_str());
}

TEST_CASE("profile import rejects user supplied routes") {
    const std::string path = "foundation-custom-routes.json";
    { std::ofstream out(path); out << R"([{"game_name":"unsafe","process_name":"game.exe","custom_routes":["203.0.113.1"]}])"; }
    gno::GameProfiles profiles;
    CHECK_FALSE(profiles.importFromFile(path));
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
