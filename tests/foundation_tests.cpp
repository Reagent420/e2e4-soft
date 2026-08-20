#include "doctest.h"
#include "core/game_profiles.h"
#include "core/input_validation.h"
#include "core/session_history.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>

namespace {

class RestoredGameProfiles {
public:
    RestoredGameProfiles() : profiles_(std::make_unique<gno::GameProfiles>()) {
        path_ = profiles_->getSavePath();
        std::ifstream input(path_, std::ios::binary);
        existed_ = static_cast<bool>(input);
        if (existed_) {
            original_.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        }
    }

    ~RestoredGameProfiles() {
        profiles_.reset();
        if (!existed_) {
            std::remove(path_.c_str());
            return;
        }

        std::ofstream output(path_, std::ios::binary | std::ios::trunc);
        if (output) output.write(original_.data(), static_cast<std::streamsize>(original_.size()));
    }

    gno::GameProfiles& get() { return *profiles_; }

private:
    std::unique_ptr<gno::GameProfiles> profiles_;
    std::string path_;
    std::string original_;
    bool existed_ = false;
};

} // namespace

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
