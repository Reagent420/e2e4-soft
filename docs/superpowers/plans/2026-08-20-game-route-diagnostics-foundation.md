# Game Route Diagnostics Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce a safe, buildable Windows/macOS diagnostic foundation with bounded structured input, owned background work, disabled mutating UI actions, and stable diagnostic interfaces.

**Architecture:** Keep the Qt 6/C++17 application and isolate platform code behind shared contracts. This first plan repairs the existing cross-platform and lifecycle defects before adding endpoint observation or measurement behavior; later plans implement local diagnostics, the Linux probe, and cross-platform packaging against these contracts.

**Tech Stack:** C++17, CMake 3.20+, Qt 6 Widgets/Charts, doctest 2.5, nlohmann/json 3.12.0, Windows IP Helper APIs, macOS public system APIs.

## Global Constraints

- Support Windows 10/11 x86-64 and macOS 13+ on Apple Silicon and Intel.
- Do not install a VPN, create a tunnel, change DNS, modify routes, terminate processes, alter registry or `sysctl`, or require administrator/root access.
- Do not assemble platform commands as shell strings.
- Do not detach background workers from their owning object.
- Bound every imported file by bytes and records and parse structured data with nlohmann/json.
- Keep the first-release game catalog bundled; do not download unsigned catalog updates.
- Hide or mark unavailable every UI action that claims to mutate DNS, routing, FPS, processes, or system settings.

---

## File Map

- `CMakeLists.txt`: dependency pinning, shared source lists, warning/sanitizer options, and CTest registration.
- `src/core/game_detector.cpp`: platform guards and portable process-name conversion.
- `src/core/network_utils.cpp`: portable nonblocking sockets and bounded port iteration.
- `src/core/input_validation.h/.cpp`: bounded integer, IPv4, and file-reading helpers.
- `src/core/game_profiles.h`: obsolete substring-parser declarations removed.
- `src/core/game_profiles.cpp`: bounded nlohmann/json profile persistence.
- `src/core/session_history.cpp`: bounded nlohmann/json history persistence without recursive locking.
- `src/ui/network_tools.h/.cpp`: owned DNS benchmark worker and verified result reporting.
- `src/ui/sidebar.h/.cpp`, `src/ui/main_window.cpp`: diagnostic-only navigation.
- `src/diagnostics/diagnostic_types.h`: immutable value types and error vocabulary.
- `src/diagnostics/endpoint_observer.h`, `network_sampler.h`, `probe_client.h`: platform-neutral interfaces used by later plans.
- `tests/foundation_tests.cpp`: validation, persistence, lifecycle-adjacent, and contract tests.
- `.github/workflows/ci.yml`: explicit CTest and sanitizer build gates.

---

### Task 1: Restore portable builds and test registration

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `src/core/game_detector.cpp`
- Modify: `src/core/network_utils.cpp`
- Modify: `src/optimization/fps_optimizer.cpp`
- Modify: `src/core/process_monitor.cpp`
- Modify: `tests/unit_tests.cpp`

**Interfaces:**
- Consumes: existing `GNO-console`, `GNO`, and `GNO-tests` CMake targets.
- Produces: buildable macOS/Linux console and test targets; Windows-only helpers compiled only under `PLATFORM_WINDOWS`; registered CTest target `GNO-UnitTests`.

- [ ] **Step 1: Reproduce the failing non-Windows build**

Run:

```bash
rtk cmake -S . -B build-foundation -DGNO_CONSOLE=ON -DGNO_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
rtk cmake --build build-foundation --parallel 4
```

Expected: FAIL in `game_detector.cpp` on `CP_UTF8`/Win32 discovery APIs and in `network_utils.cpp` on `F_SETFL`/`O_NONBLOCK`.

- [ ] **Step 2: Add the missing build and test primitives**

Update the top of `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(GNO VERSION 1.2.0 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GNO_CONSOLE "Build console-only (no Qt)" OFF)
option(GNO_TESTS "Build unit tests" ON)
option(GNO_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)

if(GNO_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()
```

Immediately after each `add_executable` block, add warnings without applying them to third-party code:

```cmake
function(gno_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

gno_enable_warnings(${PROJECT_NAME}-console)
```

Apply the same function to `${PROJECT_NAME}` when Qt is found and `${PROJECT_NAME}-tests` when tests are enabled. Keep `add_test(NAME GNO-UnitTests COMMAND ${PROJECT_NAME}-tests)` inside `if(GNO_TESTS)`; `include(CTest)` supplies `enable_testing()`.

- [ ] **Step 3: Compile Win32 conversion and discovery only on Windows**

In `src/core/game_detector.cpp`, add the missing standard header and guard conversion helpers:

```cpp
#include <map>

#ifdef PLATFORM_WINDOWS
static std::wstring toWide(const std::string& value) {
    if (value.empty()) return {};
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) return {};

    std::wstring result(static_cast<std::size_t>(required), L'\0');
    const int written = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), required);
    if (written != required) return {};
    return result;
}
#endif
```

Delete the unused `toNarrow` helper. Insert `#ifdef PLATFORM_WINDOWS` immediately after the opening brace of `getSteamLibraryFolders`, and `#endif` immediately before its existing `return folders;`. That final return produces an empty vector outside Windows:

```cpp
std::vector<std::string> GameDetector::getSteamLibraryFolders() const {
    std::vector<std::string> folders;
#ifdef PLATFORM_WINDOWS
    const std::string steamPath = getSteamPath();
    if (steamPath.empty()) return folders;
    folders.push_back(steamPath + "\\steamapps");
    const std::string vdfPath = steamPath + "\\steamapps\\libraryfolders.vdf";
    std::ifstream file(vdfPath);
    if (file.is_open()) {
        std::string line;
        const std::regex path_regex(R"rx("path"\s*"([^"]+)")rx");
        while (std::getline(file, line)) {
            std::smatch match;
            if (std::regex_search(line, match, path_regex)) {
                std::string path = match[1].str();
                std::replace(path.begin(), path.end(), '/', '\\');
                if (!path.empty() && path.back() != '\\') path += "\\";
                folders.push_back(path + "steamapps");
            }
        }
    }
#endif
    return folders;
}
```

For each `void` scanner, insert the same compile boundary around its current Win32 API statements. The exact opening and closing lines are:

```cpp
void GameDetector::scanSteamLibrary() {
#ifdef PLATFORM_WINDOWS
    const auto folders = getSteamLibraryFolders();
    if (folders.empty()) return;
    // FindFirstFileA through FindClose remain between these two preprocessor lines.
#endif
}
```

Do not create non-Windows fake installation results.

- [ ] **Step 4: Make nonblocking socket setup portable and correct**

In `src/core/network_utils.cpp`, add:

```cpp
#ifndef PLATFORM_WINDOWS
#include <fcntl.h>
#include <cerrno>
#endif
```

Replace `isPortOpen` with a zero-initialized address, validated conversion, checked nonblocking setup, and `SO_ERROR` verification:

```cpp
bool NetworkUtils::isPortOpen(const std::string& host, uint16_t port, uint32_t timeout_ms) {
    const int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
#ifdef PLATFORM_WINDOWS
        closesocket(sock);
#else
        close(sock);
#endif
        return false;
    }

#ifdef PLATFORM_WINDOWS
    u_long mode = 1;
    if (ioctlsocket(sock, FIONBIO, &mode) != 0) {
        closesocket(sock);
        return false;
    }
#else
    const int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0 || fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0) {
        close(sock);
        return false;
    }
#endif

    const int connect_result = connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
#ifndef PLATFORM_WINDOWS
    if (connect_result < 0 && errno != EINPROGRESS) {
        close(sock);
        return false;
    }
#else
    if (connect_result == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
        closesocket(sock);
        return false;
    }
#endif

    fd_set writefds;
    FD_ZERO(&writefds);
    FD_SET(sock, &writefds);
    timeval timeout{static_cast<long>(timeout_ms / 1000), static_cast<long>((timeout_ms % 1000) * 1000)};
    const int selected = select(sock + 1, nullptr, &writefds, nullptr, &timeout);

    int socket_error = 1;
#ifdef PLATFORM_WINDOWS
    int error_size = sizeof(socket_error);
    if (selected > 0) getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socket_error), &error_size);
    closesocket(sock);
#else
    socklen_t error_size = sizeof(socket_error);
    if (selected > 0) getsockopt(sock, SOL_SOCKET, SO_ERROR, &socket_error, &error_size);
    close(sock);
#endif
    return selected > 0 && socket_error == 0;
}
```

Replace the wrapping `uint16_t` loop in `scanPorts`:

```cpp
for (uint32_t value = start_port; value <= end_port; ++value) {
    const auto port = static_cast<uint16_t>(value);
    if (isPortOpen(host, port, timeout_ms)) open_ports.push_back(port);
}
```

- [ ] **Step 5: Guard remaining Windows conversion helpers**

In `src/optimization/fps_optimizer.cpp` and `src/core/process_monitor.cpp`, place each `toWide` helper inside `#ifdef PLATFORM_WINDOWS`. Use the checked implementation from Step 3. In `fps_optimizer.cpp`, keep every call to `toWide` inside its existing Windows block.

- [ ] **Step 6: Verify portable compilation and CTest discovery**

Before running CTest, make the three legacy tests that exercise Windows-only
implementations state their platform contract explicitly in `tests/unit_tests.cpp`:
on Windows retain the existing non-empty/sent-count assertions; outside Windows
assert that route discovery and process enumeration return empty collections and
that packet-loss measurement reports zero packets sent. This records current
unsupported behavior without inventing macOS functionality or silently skipping
the tests.

Run:

```bash
rtk cmake -S . -B build-foundation -DGNO_CONSOLE=ON -DGNO_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
rtk cmake --build build-foundation --parallel 4
rtk ctest --test-dir build-foundation --output-on-failure
```

Expected: build exits 0; CTest reports `GNO-UnitTests` and zero failures.

- [ ] **Step 7: Commit the portable-build repair**

```bash
rtk git add CMakeLists.txt src/core/game_detector.cpp src/core/network_utils.cpp src/optimization/fps_optimizer.cpp src/core/process_monitor.cpp tests/unit_tests.cpp
rtk git commit -m "fix: restore portable diagnostic builds"
```

---

### Task 2: Add bounded validation and structured persistence

**Files:**
- Create: `src/core/input_validation.h`
- Create: `src/core/input_validation.cpp`
- Create: `tests/foundation_tests.cpp`
- Modify: `CMakeLists.txt`
- Modify: `src/main_console.cpp`
- Modify: `src/core/game_profiles.h`
- Modify: `src/core/game_profiles.cpp`
- Modify: `src/core/session_history.cpp`

**Interfaces:**
- Produces: `gno::readBoundedFile(path, max_bytes) -> std::optional<std::string>`.
- Produces: `gno::parseBoundedInt(text, minimum, maximum) -> std::optional<int>`.
- Consumes: nlohmann/json 3.12.0 target `nlohmann_json::nlohmann_json`.

- [ ] **Step 1: Write failing validation tests**

Create `tests/foundation_tests.cpp`:

```cpp
#include "doctest.h"
#include "core/input_validation.h"
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
```

Add the file to `GNO-tests` sources. Run:

```bash
rtk cmake --build build-foundation --target GNO-tests --parallel 4
```

Expected: FAIL because `core/input_validation.h` does not exist.

- [ ] **Step 2: Pin and link nlohmann/json**

Add before target definitions in `CMakeLists.txt`:

```cmake
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)
```

Add `src/core/input_validation.cpp` to `CORE_SOURCES`, and add `nlohmann_json::nlohmann_json` to `COMMON_LIBS`.

- [ ] **Step 3: Implement bounded parsing helpers**

Create `src/core/input_validation.h`:

```cpp
#pragma once
#include <cstddef>
#include <optional>
#include <string>

namespace gno {
std::optional<int> parseBoundedInt(const std::string& text, int minimum, int maximum);
std::optional<std::string> readBoundedFile(const std::string& path, std::size_t max_bytes);
}
```

Create `src/core/input_validation.cpp`:

```cpp
#include "input_validation.h"
#include <charconv>
#include <fstream>

namespace gno {
std::optional<int> parseBoundedInt(const std::string& text, int minimum, int maximum) {
    int value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return std::nullopt;
    if (value < minimum || value > maximum) return std::nullopt;
    return value;
}

std::optional<std::string> readBoundedFile(const std::string& path, std::size_t max_bytes) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return std::nullopt;
    const auto end = file.tellg();
    if (end < 0 || static_cast<std::size_t>(end) > max_bytes) return std::nullopt;
    std::string content(static_cast<std::size_t>(end), '\0');
    file.seekg(0);
    if (!content.empty() && !file.read(content.data(), static_cast<std::streamsize>(content.size()))) return std::nullopt;
    return content;
}
}
```

- [ ] **Step 4: Validate CLI ping count before conversion**

Include `core/input_validation.h` in `src/main_console.cpp` and replace the `--ping` branch:

```cpp
} else if (arg == "--ping" && i + 1 < argc) {
    const auto parsed = gno::parseBoundedInt(argv[++i], 1, 100);
    if (!parsed) {
        std::cerr << "--ping must be an integer from 1 to 100\n";
        return 2;
    }
    ping_count = *parsed;
```

At the call site, make the checked conversion explicit:

```cpp
auto results = pinger.pingBatch(target, static_cast<uint32_t>(ping_count), 3000);
```

- [ ] **Step 5: Replace profile substring parsing with bounded JSON**

In `src/core/game_profiles.h`, delete the private declarations of `parseProfile` and `escapeJson`. In `src/core/game_profiles.cpp`, include `input_validation.h`, `<nlohmann/json.hpp>`, and `<stdexcept>`. Define limits and conversion helpers in the anonymous namespace:

```cpp
namespace {
constexpr std::size_t kMaxProfileBytes = 1024 * 1024;
constexpr std::size_t kMaxProfiles = 256;

gno::GameProfile profileFromJson(const nlohmann::json& value) {
    gno::GameProfile profile;
    profile.game_name = value.at("game_name").get<std::string>();
    profile.process_name = value.at("process_name").get<std::string>();
    profile.multipath_enabled = value.value("multipath_enabled", false);
    profile.fps_boost_enabled = value.value("fps_boost_enabled", false);
    profile.network_optimization = value.value("network_optimization", false);
    profile.max_routes = std::clamp(value.value("max_routes", 1), 1, 5);
    profile.preferred_region = value.value("preferred_region", std::string{"auto"});
    profile.priority_class = std::clamp(value.value("priority_class", 0), 0, 10);
    profile.auto_apply = value.value("auto_apply", false);
    if (profile.game_name.size() > 128 || profile.process_name.size() > 260 ||
        profile.preferred_region.size() > 64) {
        throw std::runtime_error("profile string too long");
    }
    if (value.contains("custom_routes") &&
        (!value.at("custom_routes").is_array() || !value.at("custom_routes").empty())) {
        throw std::runtime_error("imported custom routes are not permitted");
    }
    return profile;
}

nlohmann::json profileToJson(const gno::GameProfile& profile) {
    return {{"game_name", profile.game_name},
            {"process_name", profile.process_name},
            {"multipath_enabled", profile.multipath_enabled},
            {"fps_boost_enabled", profile.fps_boost_enabled},
            {"network_optimization", profile.network_optimization},
            {"max_routes", profile.max_routes},
            {"preferred_region", profile.preferred_region},
            {"priority_class", profile.priority_class},
            {"auto_apply", profile.auto_apply}};
}
}
```

Add this parser below `profileToJson`:

```cpp
std::optional<std::vector<gno::GameProfile>> parseProfilesDocument(const std::string& content) {
    try {
        const auto root = nlohmann::json::parse(content);
        const nlohmann::json* items = nullptr;
        if (root.is_array()) items = &root;
        if (root.is_object() && root.contains("profiles") && root.at("profiles").is_array()) {
            items = &root.at("profiles");
        }
        if (!items || items->size() > kMaxProfiles) return std::nullopt;
        std::vector<gno::GameProfile> result;
        result.reserve(items->size());
        for (const auto& item : *items) {
            auto profile = profileFromJson(item);
            if (profile.game_name.empty()) return std::nullopt;
            result.push_back(std::move(profile));
        }
        return result;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}
```

Replace the two input paths with:

```cpp
void GameProfiles::load() {
    const auto content = readBoundedFile(getSavePath(), kMaxProfileBytes);
    if (!content) return;
    auto parsed = parseProfilesDocument(*content);
    if (parsed) profiles_ = std::move(*parsed);
}

bool GameProfiles::importFromFile(const std::string& path) {
    const auto content = readBoundedFile(path, kMaxProfileBytes);
    if (!content) return false;
    auto parsed = parseProfilesDocument(*content);
    if (!parsed) return false;
    profiles_ = std::move(*parsed);
    save();
    return true;
}
```

Change `GameProfiles::save()` from `void` to `bool`; existing callers may ignore its result. Implement `save()` and `exportToFile()` by pushing `profileToJson(profile)` into `nlohmann::json::array()`. Write `items.dump(2)` for the local file and `nlohmann::json{{"version", 1}, {"profiles", items}}.dump(2)` for export. Return `false` if the output stream cannot be opened or written.

- [ ] **Step 6: Make session history bounded and non-recursively locked**

In `src/core/session_history.cpp`, use the same JSON dependency with:

```cpp
namespace {
constexpr std::size_t kMaxHistoryBytes = 4 * 1024 * 1024;
constexpr std::size_t kMaxHistoryRecords = 500;
}
```

Add a private `saveToFileUnlocked(const std::string&) const` declaration to `session_history.h` and use this locking boundary:

```cpp
bool SessionHistory::saveToFile(const std::string& path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return saveToFileUnlocked(path.empty() ? getSavePath() : path);
}
```

Move serialization into `saveToFileUnlocked` and emit a nlohmann/json array. `recordEnd()` and `clear()` call `saveToFileUnlocked(getSavePath())` while already holding `mutex_`; neither calls the locking public method. `loadFromFile()` uses `readBoundedFile`, requires an array of at most 500 records, assigns fields with `.value("field", default)`, catches `std::exception`, and replaces `records_` only after the complete temporary vector validates.

- [ ] **Step 7: Run validation and persistence tests**

Extend `tests/foundation_tests.cpp` with a malformed-profile regression:

```cpp
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
```

Run:

```bash
rtk cmake -S . -B build-foundation -DGNO_CONSOLE=ON -DGNO_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
rtk cmake --build build-foundation --parallel 4
rtk ctest --test-dir build-foundation --output-on-failure
```

Expected: zero build errors and zero test failures.

- [ ] **Step 8: Commit structured input handling**

```bash
rtk git add CMakeLists.txt src/main_console.cpp src/core/input_validation.h src/core/input_validation.cpp src/core/game_profiles.cpp src/core/session_history.h src/core/session_history.cpp tests/foundation_tests.cpp
rtk git commit -m "fix: bound and validate diagnostic inputs"
```

---

### Task 3: Own background work and expose diagnostic-only UI

**Files:**
- Modify: `src/ui/network_tools.h`
- Modify: `src/ui/network_tools.cpp`
- Modify: `src/ui/sidebar.h`
- Modify: `src/ui/sidebar.cpp`
- Modify: `src/ui/main_window.cpp`
- Modify: `src/ui/dashboard.h`
- Modify: `src/ui/dashboard.cpp`
- Modify: `src/ui/settings_page.h`
- Modify: `src/ui/settings_page.cpp`
- Modify: `src/ui/system_tray.h`
- Modify: `src/ui/system_tray.cpp`
- Modify: `src/main_gui.cpp`
- Modify: `src/core/dns_manager.h`
- Modify: `src/core/dns_manager.cpp`
- Modify: `src/main_console.cpp`
- Create: `tests/ui_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `NetworkToolsWidget::~NetworkToolsWidget()` that signals cancellation and joins its worker; DNS sampling observes the cancellation token between bounded probes.
- Produces: `NavPage::{Dashboard, Games, Monitoring, Diagnostics, History, Settings, Count}` with contiguous stack indices.
- Removes mutating CLI entry points `--boost`, `--game`, and `--dns-apply`; `--watch` remains observational and sets `auto_apply_profiles = false`.

- [ ] **Step 1: Add a compile-time diagnostic navigation test**

Create `tests/ui_tests.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "ui/sidebar.h"

TEST_CASE("diagnostic navigation has no mutating pages") {
    CHECK(static_cast<int>(gno::NavPage::Dashboard) == 0);
    CHECK(static_cast<int>(gno::NavPage::Diagnostics) == 3);
    CHECK(static_cast<int>(gno::NavPage::Count) == 6);
}
```

Inside the existing `if(Qt6_FOUND)` block, add a dedicated UI test target:

```cmake
if(GNO_TESTS)
    add_executable(${PROJECT_NAME}-ui-tests tests/ui_tests.cpp src/ui/sidebar.cpp)
    target_include_directories(${PROJECT_NAME}-ui-tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
    target_link_libraries(${PROJECT_NAME}-ui-tests PRIVATE Qt6::Widgets)
    gno_enable_warnings(${PROJECT_NAME}-ui-tests)
    add_test(NAME GNO-UITests COMMAND ${PROJECT_NAME}-ui-tests)
endif()
```

Run the GUI test build.

Expected: FAIL because `NavPage::Diagnostics` does not exist and `Count` is 10.

- [ ] **Step 2: Replace detached DNS work with an owned thread**

In `src/ui/network_tools.h`, add:

```cpp
#include <atomic>
#include <thread>

public:
    ~NetworkToolsWidget() override;

private:
    void stopDnsWorker();
    std::thread m_dnsWorker;
    std::atomic<bool> m_stopping{false};
    std::atomic<bool> m_dnsRunning{false};
```

In `src/ui/network_tools.cpp`, implement:

```cpp
NetworkToolsWidget::~NetworkToolsWidget() {
    stopDnsWorker();
    delete m_speedTest;
    delete m_dnsManager;
}

void NetworkToolsWidget::stopDnsWorker() {
    m_stopping = true;
    if (m_dnsWorker.joinable()) m_dnsWorker.join();
}
```

Include `<QPointer>`. At the start of `runDNSBenchmark`, use `if (m_dnsRunning.exchange(true)) return;`, join a previously completed joinable worker, and launch into `m_dnsWorker` rather than detaching. Capture `QPointer<NetworkToolsWidget> owner(this)`. Before queueing the UI update, check `m_stopping`; check `owner` again inside the queued lambda. Set `m_dnsRunning = false` on every worker exit and remove `.detach()`.

Extend `DNSManager::benchmarkServer` and `benchmarkAll` with an optional `const std::atomic<bool>* cancellation` parameter. Check it before each server and before each bounded ICMP attempt; pass `&m_stopping` from the widget worker. Keep each individual platform call bounded so joining cannot wait indefinitely.

- [ ] **Step 3: Stop claiming that DNS changes were applied**

Remove the `applyDNS` slot and every call to `m_dnsManager->applyDNS` or `resetToDHCP` from the widget. Remove those two mutating methods from `DNSManager` as well, so they are absent from the diagnostic binary API. Replace each result-row `Применить` button with non-interactive text `Только замер`; remove the DHCP reset button entirely. Rename the section/title/subtitle so they say `Диагностика сети`, `Диагностика DNS`, and explicitly state that system settings are not changed.

- [ ] **Step 4: Reduce navigation to non-mutating pages**

Replace the enum in `src/ui/sidebar.h`:

```cpp
enum class NavPage {
    Dashboard = 0,
    Games,
    Monitoring,
    Diagnostics,
    History,
    Settings,
    Count
};
```

In `src/ui/sidebar.cpp`, create exactly six buttons with IDs 0 through 5 and labels `Главная`, `Игры`, `Мониторинг`, `Диагностика маршрута`, `История`, `Настройки`. Reuse the existing `NetworkTools` globe icon drawing for `Diagnostics`, and remove switch cases for hidden mutating pages.

In `src/ui/main_window.cpp`, add pages in the identical order. Until the diagnostics page is implemented in the next plan, use a `NetworkToolsWidget` at index 3 because it is now read-only. Do not instantiate `GameProfilesWidget`, `OptimizerWidget`, `ProcessMonitorWidget`, or `GeoMapWidget` in the diagnostic release.

Remove those four widgets' headers from `main_window.cpp` and their `.cpp` files from `UI_SOURCES`; otherwise the diagnostic GUI still compiles and links hidden mutating/broken pages. Keep their source files in the repository for later migration, but do not include them in the release target.

Remove the dashboard's fake optimization button, `boostToggled` signal, slot, and state. Queue ping-result UI updates onto the Qt object thread through a `QPointer<DashboardWidget>` instead of mutating widgets from `PingMonitor`'s worker callback. In the settings page, remove or disable unsupported system-changing controls and rewrite the About copy to describe route diagnostics only; it must not claim FPS, DNS application, multipath routing, process termination, or automatic optimization. Remove the system-tray optimization action/signal/state and its connection in `main_gui.cpp`. Change remaining product copy from “optimizer/optimization” to “game route diagnostics.”

In `src/main_console.cpp`, remove `--boost`, `--game`, and `--dns-apply` from help, argument parsing, state, and execution. They must fall through to the existing unknown-option error. Keep `--watch` observational: describe it as game detection, set `auto_apply_profiles = false`, and remove auto-apply claims/messages. Remove now-unused optimizer includes. Add CTest cases for the removed flags and mark them `WILL_FAIL TRUE` so a zero exit would fail CI.

Remove `${OPT_SOURCES}` and `${PLATFORM_SOURCES}` from console, GUI, and test target source lists, and remove the default console's FPS-optimizer inspection block. The legacy mutator sources remain in the repository for a later release but must not be linked into diagnostic release binaries.

- [ ] **Step 5: Verify lifecycle and navigation**

Run:

```bash
rtk cmake -S . -B build-foundation-gui -DGNO_CONSOLE=OFF -DGNO_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
rtk cmake --build build-foundation-gui --parallel 4
rtk ctest --test-dir build-foundation-gui --output-on-failure
```

Expected: GUI and tests build; navigation and rejected-mutating-CLI tests pass; closing a widget during a DNS benchmark completes without a detached thread, an unbounded join, or access to destroyed UI state.

- [ ] **Step 6: Commit diagnostic-only lifecycle changes**

```bash
rtk git add CMakeLists.txt src/ui/network_tools.h src/ui/network_tools.cpp src/ui/sidebar.h src/ui/sidebar.cpp src/ui/main_window.cpp src/ui/dashboard.h src/ui/dashboard.cpp src/ui/settings_page.h src/ui/settings_page.cpp src/ui/system_tray.h src/ui/system_tray.cpp src/main_gui.cpp src/core/dns_manager.h src/core/dns_manager.cpp src/main_console.cpp tests/ui_tests.cpp
rtk git commit -m "fix: own workers and expose diagnostic-only UI"
```

---

### Task 4: Define stable diagnostic contracts

**Files:**
- Create: `src/diagnostics/diagnostic_types.h`
- Create: `src/diagnostics/diagnostic_types.cpp`
- Create: `src/diagnostics/endpoint_observer.h`
- Create: `src/diagnostics/network_sampler.h`
- Create: `src/diagnostics/probe_client.h`
- Modify: `CMakeLists.txt`
- Test: `tests/foundation_tests.cpp`

**Interfaces:**
- Produces: immutable-by-convention data structures used by all later diagnostic tasks.
- Produces: `IEndpointObserver::observe(uint32_t, std::chrono::milliseconds)`.
- Produces: `INetworkSampler::sample(const SampleTarget&, const SamplePlan&, const CancellationToken&)`.
- Produces: `IProbeClient::measure(const ProbeRequest&, const CancellationToken&)`.

- [ ] **Step 1: Write contract tests before headers exist**

Add to `tests/foundation_tests.cpp`:

```cpp
#include "diagnostics/diagnostic_types.h"

TEST_CASE("diagnostic defaults are safe") {
    gno::DiagnosticReport report;
    CHECK(report.outcome == gno::DiagnosticOutcome::InsufficientData);
    CHECK(report.confidence == gno::ConfidenceLevel::Low);
    CHECK(report.network_settings_changed == false);
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

TEST_CASE("cancellation token owns its state") {
    gno::CancellationToken token;
    {
        gno::CancellationSource source;
        token = source.token();
        source.cancel();
    }
    CHECK(token.isCancelled());
}
```

Run the test build.

Expected: FAIL because `diagnostics/diagnostic_types.h` does not exist.

- [ ] **Step 2: Create the shared value vocabulary**

Create `src/diagnostics/diagnostic_types.h` with:

```cpp
#pragma once
#include <atomic>
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gno {
enum class DiagnosticError {
    None, PermissionDenied, UnsupportedCapability, GameNotDetected,
    EndpointNotObserved, EndpointNotAllowlisted, ProbeUnavailable,
    Timeout, InsufficientResponses, MalformedResponse, InternalFailure
};
enum class DiagnosticOutcome { ImprovementLikely, NoImprovementFound, LocalNetworkProblem, InsufficientData };
enum class ConfidenceLevel { Low, Medium, High };
enum class TransportProtocol { Tcp, Udp };

class CancellationToken {
public:
    CancellationToken() = default;
    bool isCancelled() const noexcept;
private:
    explicit CancellationToken(std::shared_ptr<const std::atomic<bool>> state);
    std::shared_ptr<const std::atomic<bool>> state_;
    friend class CancellationSource;
};

class CancellationSource {
public:
    CancellationSource();
    CancellationToken token() const;
    void cancel() noexcept;
private:
    std::shared_ptr<std::atomic<bool>> state_;
};

class Ipv4Address {
public:
    Ipv4Address() = default; // 0.0.0.0 means unspecified in default reports/targets
    static std::optional<Ipv4Address> parse(std::string_view text) noexcept;
    std::string toString() const;
    const std::array<uint8_t, 4>& bytes() const noexcept { return bytes_; }
    bool isUnspecified() const noexcept { return bytes_ == std::array<uint8_t, 4>{}; }
    friend bool operator==(const Ipv4Address& left, const Ipv4Address& right) noexcept {
        return left.bytes_ == right.bytes_;
    }
private:
    explicit Ipv4Address(std::array<uint8_t, 4> bytes) : bytes_(bytes) {}
    std::array<uint8_t, 4> bytes_{};
};

struct ObservedEndpoint {
    Ipv4Address ip;
    uint16_t port = 0;
    TransportProtocol protocol = TransportProtocol::Udp;
    uint32_t owner_pid = 0;
    uint64_t observed_packets = 0;
};

struct SampleTarget {
    Ipv4Address ip;
    uint16_t port = 0;
    TransportProtocol protocol = TransportProtocol::Udp;
};

struct SamplePlan {
    uint32_t duration_seconds = 30;
    uint32_t interval_ms = 1000;
    uint32_t timeout_ms = 1000;
};

struct MetricSummary {
    uint32_t sent = 0;
    uint32_t received = 0;
    double median_ms = 0.0;
    double p95_ms = 0.0;
    double jitter_ms = 0.0;
    double loss_percent = 0.0;
};

struct ProbeRequest {
    std::string game_id;
    Ipv4Address endpoint_ip;
    uint16_t endpoint_port = 0;
    TransportProtocol protocol = TransportProtocol::Udp;
    uint32_t duration_seconds = 30;
};

struct ProbeMeasurement {
    std::string probe_region;
    MetricSummary client_to_probe;
    MetricSummary probe_to_game;
    DiagnosticError error = DiagnosticError::None;
};

struct DiagnosticReport {
    DiagnosticOutcome outcome = DiagnosticOutcome::InsufficientData;
    ConfidenceLevel confidence = ConfidenceLevel::Low;
    std::string game_id;
    ObservedEndpoint endpoint;
    MetricSummary gateway;
    MetricSummary direct;
    std::vector<ProbeMeasurement> candidates;
    std::vector<std::string> evidence;
    static constexpr bool network_settings_changed = false;
};
}
```

Implement `diagnostic_types.cpp` without platform socket APIs: parse exactly four decimal octets with no empty components, signs, whitespace, trailing data, or values above 255. Format from the stored octets. `CancellationSource` owns a shared atomic initialized to false; tokens retain shared const ownership after the source is destroyed, and `cancel()` stores true.

- [ ] **Step 3: Define platform-neutral service contracts**

Create `src/diagnostics/endpoint_observer.h`:

```cpp
#pragma once
#include "diagnostic_types.h"
#include <chrono>
#include <vector>
namespace gno {
class IEndpointObserver {
public:
    virtual ~IEndpointObserver() = default;
    virtual std::vector<ObservedEndpoint> observe(
        uint32_t pid, std::chrono::milliseconds window, DiagnosticError& error) = 0;
};
}
```

Create `src/diagnostics/network_sampler.h`:

```cpp
#pragma once
#include "diagnostic_types.h"
namespace gno {
class INetworkSampler {
public:
    virtual ~INetworkSampler() = default;
    virtual MetricSummary sample(const SampleTarget& target, const SamplePlan& plan,
                                 const CancellationToken& cancellation,
                                 DiagnosticError& error) = 0;
};
}
```

Create `src/diagnostics/probe_client.h`:

```cpp
#pragma once
#include "diagnostic_types.h"
namespace gno {
class IProbeClient {
public:
    virtual ~IProbeClient() = default;
    virtual ProbeMeasurement measure(const ProbeRequest& request,
                                     const CancellationToken& cancellation) = 0;
};
}
```

Add `src/diagnostics/diagnostic_types.cpp` to `CORE_SOURCES` and `${CMAKE_CURRENT_SOURCE_DIR}/src/diagnostics` to client and test include directories.

- [ ] **Step 4: Run contract tests**

```bash
rtk cmake --build build-foundation --parallel 4
rtk ctest --test-dir build-foundation --output-on-failure
```

Expected: all contract and existing unit tests pass.

- [ ] **Step 5: Commit diagnostic interfaces**

```bash
rtk git add CMakeLists.txt src/diagnostics tests/foundation_tests.cpp
rtk git commit -m "feat: define diagnostic service contracts"
```

---

### Task 5: Add build gates for the foundation

**Files:**
- Modify: `.github/workflows/ci.yml`
- Modify: `CMakeLists.txt`
- Create: `docs/development/diagnostic-builds.md`

**Interfaces:**
- Consumes: CMake presets from Tasks 1–4.
- Produces: Windows build/test gate, macOS build/test gate, Linux sanitizer gate.

- [ ] **Step 1: Demonstrate the missing macOS gate**

Run:

```bash
rtk rg -n "macos-latest|GNO_SANITIZERS|ctest" .github/workflows/ci.yml
```

Expected: no `macos-latest` job, no sanitizer configuration, and direct test executable invocation rather than CTest.

- [ ] **Step 2: Replace CI with explicit platform gates**

Keep the existing Windows MSYS2 dependency installation, configure explicitly with `-DGNO_TESTS=ON`, and build the default target so `GNO-ui-tests` is not skipped. Change test execution to:

```yaml
- name: Run tests
  shell: msys2 {0}
  run: ctest --test-dir build --output-on-failure
```

Add a native macOS matrix covering both supported architectures. The current official GitHub runner labels are `macos-15` for Apple Silicon and `macos-15-intel` for x86-64:

```yaml
build-macos:
  strategy:
    fail-fast: false
    matrix:
      runner: [macos-15, macos-15-intel]
  runs-on: ${{ matrix.runner }}
  timeout-minutes: 30
  steps:
    - uses: actions/checkout@v4
    - name: Install Qt
      run: brew install qt
    - name: Configure
      run: cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DGNO_TESTS=ON -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
    - name: Build
      run: cmake --build build --parallel 4
    - name: Test
      run: ctest --test-dir build --output-on-failure
```

Change the Linux configure command to enable sanitizers and console-only mode:

```yaml
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DGNO_CONSOLE=ON -DGNO_TESTS=ON -DGNO_SANITIZERS=ON
```

The Linux job does not install Qt because console-only mode must not discover or build GUI targets. Build every configured default target and run all tests with CTest.

Do not broaden workflow permissions; add `permissions: contents: read` at workflow level. Set `persist-credentials: false` on every checkout step. Keep third-party action references on explicit released major versions rather than branches.

In `CMakeLists.txt`, silence project-owned configuration noise: add `DOWNLOAD_EXTRACT_TIMESTAMP TRUE` to the pinned FetchContent declaration, disable global AUTOMOC, and enable AUTOMOC only on the GUI and UI-test targets that contain `Q_OBJECT`. Console-only configuration must not emit Qt AUTOGEN warnings.

- [ ] **Step 3: Document exact local build commands**

Create `docs/development/diagnostic-builds.md` containing the Windows, Linux sanitizer, macOS Apple Silicon, and macOS Intel configure/build/test sequences used by CI, the Qt 6 requirement for GUI builds, the macOS 13 deployment target, and this release invariant:

```text
The diagnostic build performs no privileged or mutating network operation.
Tests must run through CTest on every supported platform.
```

- [ ] **Step 4: Validate workflow syntax and local sanitizer build**

Run:

```bash
rtk cmake -S . -B build-foundation-asan -DGNO_CONSOLE=ON -DGNO_TESTS=ON -DGNO_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
rtk cmake --build build-foundation-asan --parallel 4
rtk ctest --test-dir build-foundation-asan --output-on-failure
rtk ruby -e 'require "yaml"; YAML.load_file(".github/workflows/ci.yml"); puts "workflow syntax ok"'
rtk git diff --check
```

Expected: configuration and build exit 0, CTest reports zero failures, and `git diff --check` reports no whitespace errors.

- [ ] **Step 5: Commit foundation build gates**

```bash
rtk git add .github/workflows/ci.yml CMakeLists.txt docs/development/diagnostic-builds.md
rtk git commit -m "ci: gate diagnostic foundation builds"
```

---

## Foundation Completion Gate

Before beginning the local diagnostic engine plan, run:

```bash
rtk cmake -S . -B build-foundation-final -DGNO_CONSOLE=ON -DGNO_TESTS=ON -DGNO_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
rtk cmake --build build-foundation-final --parallel 4
rtk ctest --test-dir build-foundation-final --output-on-failure
rtk git status --short
```

Required result: build and CTest exit 0; the worktree is clean; all five task commits are present. The next plan may then implement endpoint observation, direct/gateway sampling, deterministic scoring, and the diagnostic report UI against the contracts defined here.
