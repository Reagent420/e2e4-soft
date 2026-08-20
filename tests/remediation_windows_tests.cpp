#include "doctest.h"

#include "remediation/platform_action_factory.h"
#include "remediation/windows/windows_fix_action.h"
#include "remediation/windows/windows_state_api.h"

#include <array>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace {

template <typename T>
gno::Result<T> success(T value) {
    return {std::move(value), gno::RemediationError::None, {}};
}

template <typename T>
gno::Result<T> failure(gno::RemediationError error, std::string detail) {
    return {T{}, error, std::move(detail)};
}

gno::Ipv4Address ipv4(const char* text) {
    const auto parsed = gno::Ipv4Address::parse(text);
    REQUIRE(parsed.has_value());
    return *parsed;
}

class FakeWindowsStateApi final : public gno::WindowsStateApi {
public:
    gno::DnsValue dns{true, {}};
    gno::MtuValue mtu{1400};
    gno::PowerPlanValue power{"381b4222-f694-41f0-9685-ff5bb260df2e"};
    gno::FullscreenValue fullscreen{};
    gno::PriorityValue priority = gno::PriorityValue::Normal;
    std::map<gno::AllowedRegistryKey, gno::RegistryValue> registry;
    gno::RemediationError read_error = gno::RemediationError::None;
    gno::RemediationError write_error = gno::RemediationError::None;
    bool ignore_writes = false;
    bool stale_interface = false;
    bool stale_executable = false;
    bool process_exited = false;
    bool pid_reused = false;
    int writes = 0;
    int write_attempts = 0;
    int fail_once_on_write = 0;
    int corrupt_once_on_write = 0;

    gno::Result<gno::DnsValue> getDns(const gno::InterfaceId&) const override {
        return read(dns);
    }
    gno::Result<std::monostate> setDns(
        const gno::InterfaceId&, const gno::DnsValue& value) override {
        if (stale_interface) return failure<std::monostate>(gno::RemediationError::InvalidTarget, "stale interface");
        return write(dns, value);
    }
    gno::Result<gno::MtuValue> getMtu(const gno::InterfaceId&) const override {
        if (stale_interface) return failure<gno::MtuValue>(gno::RemediationError::InvalidTarget, "stale interface");
        return read(mtu);
    }
    gno::Result<std::monostate> setMtu(
        const gno::InterfaceId&, gno::MtuValue value) override {
        if (stale_interface) return failure<std::monostate>(gno::RemediationError::InvalidTarget, "stale interface");
        return write(mtu, value);
    }
    gno::Result<gno::RegistryValue> getAllowedRegistry(
        gno::AllowedRegistryKey key) const override {
        const auto found = registry.find(key);
        return read(found == registry.end() ? gno::RegistryValue{} : found->second);
    }
    gno::Result<std::monostate> setAllowedRegistry(
        gno::AllowedRegistryKey key, const gno::RegistryValue& value) override {
        ++write_attempts;
        if (write_attempts == fail_once_on_write) {
            return failure<std::monostate>(gno::RemediationError::ApplyFailed,
                                           "injected registry write failure");
        }
        if (write_error != gno::RemediationError::None) {
            return failure<std::monostate>(write_error, "registry write failed");
        }
        ++writes;
        if (!ignore_writes) registry[key] = value;
        return success(std::monostate{});
    }
    gno::Result<gno::PowerPlanValue> getPowerPlan() const override {
        return read(power);
    }
    gno::Result<std::monostate> setPowerPlan(
        const gno::PowerPlanValue& value) override {
        return write(power, value);
    }
    gno::Result<gno::FullscreenValue> getFullscreenOptimizations(
        const gno::ExecutableIdentity&) const override {
        if (stale_executable) return failure<gno::FullscreenValue>(gno::RemediationError::InvalidTarget, "stale executable");
        return read(fullscreen);
    }
    gno::Result<std::monostate> setFullscreenOptimizations(
        const gno::ExecutableIdentity&, const gno::FullscreenValue& value) override {
        if (stale_executable) return failure<std::monostate>(gno::RemediationError::InvalidTarget, "stale executable");
        return write(fullscreen, value);
    }
    gno::Result<gno::PriorityValue> getPriority(
        const gno::ProcessIdentity&) const override {
        if (process_exited) return failure<gno::PriorityValue>(gno::RemediationError::InvalidTarget, "process exited");
        if (pid_reused) return failure<gno::PriorityValue>(gno::RemediationError::InvalidTarget, "PID reused");
        return read(priority);
    }
    gno::Result<std::monostate> setPriority(
        const gno::ProcessIdentity&, gno::PriorityValue value) override {
        if (process_exited || pid_reused) return failure<std::monostate>(gno::RemediationError::InvalidTarget, "stale process");
        return write(priority, value);
    }

private:
    template <typename T>
    gno::Result<T> read(const T& value) const {
        if (read_error != gno::RemediationError::None) {
            return failure<T>(read_error, "read failed");
        }
        return success(value);
    }

    template <typename T>
    gno::Result<std::monostate> write(T& state, const T& value) {
        if (write_error != gno::RemediationError::None) {
            return failure<std::monostate>(write_error, "write failed");
        }
        ++writes;
        if (!ignore_writes) {
            if constexpr (std::is_same<T, gno::MtuValue>::value) {
                state = writes == corrupt_once_on_write
                            ? gno::MtuValue{value.bytes - 1}
                            : value;
            } else {
                state = value;
            }
        }
        return success(std::monostate{});
    }
};

const gno::InterfaceId kInterface{
    "{01234567-89ab-4cde-8f01-23456789abcd}", 55};
const gno::ExecutableIdentity kExecutable{"C:\\Games\\Example\\game.exe"};
const gno::ProcessIdentity kProcess{42, 123456, "C:\\Games\\Example\\game.exe"};

gno::ActionState observe(gno::FixAction& action, const gno::ActionTarget& target) {
    const auto result = action.observe(target, {});
    REQUIRE(result.ok());
    return result.value;
}

gno::PreparedAction prepare(gno::FixAction& action, const gno::ActionTarget& target) {
    const auto current = observe(action, target);
    const auto result = action.prepare(target, current);
    REQUIRE(result.ok());
    return result.value;
}

void checkRoundTrip(gno::FixAction& action, const gno::ActionTarget& target) {
    const auto prepared = prepare(action, target);
    const auto applied = action.apply(prepared, {});
    REQUIRE(applied.ok());
    CHECK(applied.value.value == prepared.proposed.value);
    const auto rolled_back = action.rollback(prepared, {});
    REQUIRE(rolled_back.ok());
    CHECK(rolled_back.value.value == prepared.before.value);
}

} // namespace

TEST_SUITE_BEGIN("windows remediation");

TEST_CASE("seven allowlisted actions apply verify and rollback exact typed state") {
    auto api = std::make_shared<FakeWindowsStateApi>();
    api->registry[gno::AllowedRegistryKey::TcpInitialRetransmissionTimeout] = {true, int64_t{3000}};
    api->registry[gno::AllowedRegistryKey::GameDvrEnabled] = {true, uint32_t{1}};
    api->registry[gno::AllowedRegistryKey::AppCaptureEnabled] = {false, {}};

    auto power = gno::makePowerPlanAction(api, {"8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c"});
    auto game_dvr = gno::makeGameDvrAction(
        api, {{true, uint32_t{0}}, {true, uint32_t{0}}});
    auto fullscreen = gno::makeFullscreenOptimizationsAction(
        api, {true, "~ DISABLEDXMAXIMIZEDWINDOWEDMODE"});
    auto tcp = gno::makeTcpParametersAction(api, {{
        {gno::TcpParameter::InitialRetransmissionTimeout, true, 5000}}});
    auto dns = gno::makeDnsAction(api, {false, {ipv4("1.1.1.1"), ipv4("1.0.0.1")}});
    auto mtu = gno::makeMtuAction(api, {1500});
    auto priority = gno::makePriorityAction(api, gno::PriorityValue::AboveNormal);

    checkRoundTrip(*power, std::monostate{});
    checkRoundTrip(*game_dvr, std::monostate{});
    checkRoundTrip(*fullscreen, kExecutable);
    checkRoundTrip(*tcp, std::monostate{});
    checkRoundTrip(*dns, kInterface);
    checkRoundTrip(*mtu, kInterface);
    checkRoundTrip(*priority, kProcess);
}

TEST_CASE("factory exposes exactly seven actions in transaction order") {
    auto api = std::make_shared<FakeWindowsStateApi>();
    const auto actions = gno::createWindowsFixActions(api);
    REQUIRE(actions.size() == 7);
    const std::array<gno::ActionId, 7> expected{
        gno::ActionId::PowerPlan, gno::ActionId::GameDvr,
        gno::ActionId::FullscreenOptimizations, gno::ActionId::TcpParameters,
        gno::ActionId::Dns, gno::ActionId::Mtu, gno::ActionId::ProcessPriority};
    for (std::size_t index = 0; index < expected.size(); ++index) {
        CHECK(actions[index]->id() == expected[index]);
    }
}

TEST_CASE("invalid targets and values are rejected before mutation") {
    auto api = std::make_shared<FakeWindowsStateApi>();
    auto dns = gno::makeDnsAction(api, {false, {gno::Ipv4Address{}}});
    auto low_mtu = gno::makeMtuAction(api, {575});
    auto high_mtu = gno::makeMtuAction(api, {9001});
    auto unknown_tcp = gno::makeTcpParametersAction(
        api, {{{static_cast<gno::TcpParameter>(99), true, 1}}});
    auto unapproved_tcp = gno::makeTcpParametersAction(
        api, {{{gno::TcpParameter::AutoTuningLevel, true, 1}}});
    auto realtime = gno::makePriorityAction(api, gno::PriorityValue::Realtime);
    auto invalid_registry = gno::makeGameDvrAction(
        api, {{true, std::string{"0"}}, {true, uint32_t{0}}});

    const gno::ActionState dns_current{gno::ActionId::Dns, gno::ActionStatus::Recommended, api->dns, {}};
    const gno::ActionState mtu_current{gno::ActionId::Mtu, gno::ActionStatus::Recommended, api->mtu, {}};
    const gno::ActionState tcp_current{gno::ActionId::TcpParameters, gno::ActionStatus::Recommended, gno::TcpValue{}, {}};
    const gno::ActionState priority_current{gno::ActionId::ProcessPriority, gno::ActionStatus::Recommended, api->priority, {}};

    CHECK_FALSE(dns->prepare(kInterface, dns_current).ok());
    CHECK_FALSE(low_mtu->prepare(kInterface, mtu_current).ok());
    CHECK_FALSE(high_mtu->prepare(kInterface, mtu_current).ok());
    CHECK_FALSE(unknown_tcp->prepare(std::monostate{}, tcp_current).ok());
    CHECK_FALSE(unapproved_tcp->prepare(std::monostate{}, tcp_current).ok());
    CHECK_FALSE(realtime->prepare(kProcess, priority_current).ok());
    CHECK_FALSE(invalid_registry->prepare(
        std::monostate{}, {gno::ActionId::GameDvr,
                           gno::ActionStatus::Recommended,
                           gno::GameDvrValue{{true, uint32_t{1}},
                                             {true, uint32_t{1}}}, {}}).ok());
    CHECK_FALSE(gno::makeMtuAction(api, {1500})->prepare(
        gno::InterfaceId{"Ethernet", 55}, mtu_current).ok());
    CHECK_FALSE(gno::makeFullscreenOptimizationsAction(api, {true, "~ DISABLEDXMAXIMIZEDWINDOWEDMODE"})
                    ->prepare(gno::ExecutableIdentity{"..\\game.exe"},
                              {gno::ActionId::FullscreenOptimizations,
                               gno::ActionStatus::Recommended, api->fullscreen, {}}).ok());
    CHECK(api->writes == 0);
}

TEST_CASE("stale state permission denial verify mismatch and cancellation fail closed") {
    auto api = std::make_shared<FakeWindowsStateApi>();
    auto mtu = gno::makeMtuAction(api, {1500});
    const auto prepared = prepare(*mtu, kInterface);

    api->mtu = {1450};
    const auto stale = mtu->apply(prepared, {});
    CHECK_FALSE(stale.ok());
    CHECK(stale.error == gno::RemediationError::PreflightFailed);
    CHECK(api->writes == 0);

    api->mtu = std::get<gno::MtuValue>(prepared.before.value);
    api->write_error = gno::RemediationError::PermissionDenied;
    const auto denied = mtu->apply(prepared, {});
    CHECK_FALSE(denied.ok());
    CHECK(denied.error == gno::RemediationError::PermissionDenied);

    api->write_error = gno::RemediationError::None;
    api->corrupt_once_on_write = api->writes + 1;
    const auto mismatch = mtu->apply(prepared, {});
    CHECK_FALSE(mismatch.ok());
    CHECK(mismatch.error == gno::RemediationError::VerificationMismatch);
    CHECK(api->mtu == std::get<gno::MtuValue>(prepared.before.value));

    gno::CancellationSource source;
    source.cancel();
    const int writes = api->writes;
    const auto cancelled = mtu->apply(prepared, source.token());
    CHECK_FALSE(cancelled.ok());
    CHECK(cancelled.error == gno::RemediationError::Cancelled);
    CHECK(api->writes == writes);
}

TEST_CASE("Game DVR action compensates an already-written prefix when a later write fails") {
    auto api = std::make_shared<FakeWindowsStateApi>();
    api->registry[gno::AllowedRegistryKey::GameDvrEnabled] = {true, uint32_t{1}};
    api->registry[gno::AllowedRegistryKey::AppCaptureEnabled] = {false, {}};
    const auto before = api->registry;
    auto game_dvr = gno::makeGameDvrAction(api);
    const auto prepared = prepare(*game_dvr, std::monostate{});
    api->fail_once_on_write = 2;

    const auto result = game_dvr->apply(prepared, {});

    CHECK_FALSE(result.ok());
    CHECK(result.error == gno::RemediationError::ApplyFailed);
    CHECK(api->registry == before);
}

TEST_CASE("stale interface executable and process identity are rejected") {
    auto api = std::make_shared<FakeWindowsStateApi>();
    auto mtu = gno::makeMtuAction(api, {1500});
    auto fullscreen = gno::makeFullscreenOptimizationsAction(
        api, {true, "~ DISABLEDXMAXIMIZEDWINDOWEDMODE"});
    auto priority = gno::makePriorityAction(api, gno::PriorityValue::AboveNormal);

    api->stale_interface = true;
    CHECK(mtu->observe(kInterface, {}).error == gno::RemediationError::InvalidTarget);
    api->stale_interface = false;
    api->stale_executable = true;
    CHECK(fullscreen->observe(kExecutable, {}).error == gno::RemediationError::InvalidTarget);
    api->stale_executable = false;
    api->process_exited = true;
    CHECK(priority->observe(kProcess, {}).error == gno::RemediationError::InvalidTarget);
    api->process_exited = false;
    api->pid_reused = true;
    CHECK(priority->observe(kProcess, {}).error == gno::RemediationError::InvalidTarget);
}

TEST_CASE("unsupported adapter result is preserved by an action") {
    auto api = std::make_shared<FakeWindowsStateApi>();
    api->read_error = gno::RemediationError::Unsupported;
    auto dns = gno::makeDnsAction(api, {false, {ipv4("1.1.1.1")}});
    const auto result = dns->observe(kInterface, {});
    CHECK_FALSE(result.ok());
    CHECK(result.error == gno::RemediationError::Unsupported);
}

#ifndef _WIN32
TEST_CASE("non-Windows platform factory exposes seven typed unsupported actions") {
    const auto actions = gno::createPlatformFixActions();
    REQUIRE(actions.size() == 7);
    const std::array<gno::ActionTarget, 7> targets{
        std::monostate{}, std::monostate{}, kExecutable, std::monostate{},
        kInterface, kInterface, kProcess};
    for (std::size_t index = 0; index < actions.size(); ++index) {
        const auto result = actions[index]->observe(targets[index], {});
        CHECK_FALSE(result.ok());
        CHECK(result.error == gno::RemediationError::Unsupported);
        CHECK(result.value.status == gno::ActionStatus::Unsupported);
    }
}
#endif

TEST_SUITE_END();
