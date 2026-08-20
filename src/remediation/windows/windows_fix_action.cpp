#include "remediation/windows/windows_fix_action.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <set>
#include <utility>

namespace gno {
namespace {

template <typename T>
Result<T> success(T value) {
    return {std::move(value), RemediationError::None, {}};
}

template <typename T>
Result<T> failure(RemediationError error, std::string detail, T value = {}) {
    return {std::move(value), error, std::move(detail)};
}

bool isHex(char value) noexcept {
    return std::isxdigit(static_cast<unsigned char>(value)) != 0;
}

bool isGuid(std::string_view value, bool braces_required) noexcept {
    std::size_t offset = 0;
    if (value.size() == 38 && value.front() == '{' && value.back() == '}') {
        offset = 1;
    } else if (braces_required || value.size() != 36) {
        return false;
    }
    static constexpr std::array<std::size_t, 4> hyphens{8, 13, 18, 23};
    for (std::size_t index = 0; index < 36; ++index) {
        const bool hyphen = std::find(hyphens.begin(), hyphens.end(), index) != hyphens.end();
        const char character = value[index + offset];
        if ((hyphen && character != '-') || (!hyphen && !isHex(character))) return false;
    }
    return true;
}

bool isCanonicalExecutable(const ExecutableIdentity& executable) noexcept {
    const auto& path = executable.canonical_path;
    if (path.size() < 7 || path.size() > kMaxExecutablePathLength ||
        !std::isalpha(static_cast<unsigned char>(path[0])) || path[1] != ':' ||
        (path[2] != '\\' && path[2] != '/') || path.find("..") != std::string::npos) {
        return false;
    }
    std::string suffix = path.substr(path.size() - 4);
    std::transform(suffix.begin(), suffix.end(), suffix.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return suffix == ".exe";
}

bool validTarget(ActionId id, const ActionTarget& target) noexcept {
    switch (id) {
    case ActionId::Dns:
    case ActionId::Mtu: {
        const auto* interface_id = std::get_if<InterfaceId>(&target);
        return interface_id && interface_id->luid != 0 &&
               isGuid(interface_id->value, true);
    }
    case ActionId::FullscreenOptimizations: {
        const auto* executable = std::get_if<ExecutableIdentity>(&target);
        return executable && isCanonicalExecutable(*executable);
    }
    case ActionId::ProcessPriority: {
        const auto* process = std::get_if<ProcessIdentity>(&target);
        return process && process->pid != 0 && process->creation_time != 0 &&
               isCanonicalExecutable({process->executable_path});
    }
    case ActionId::PowerPlan:
    case ActionId::GameDvr:
    case ActionId::TcpParameters:
        return std::holds_alternative<std::monostate>(target);
    case ActionId::EnergyMode:
        return false;
    }
    return false;
}

bool validDns(const DnsValue& value) noexcept {
    if (value.servers.size() > kMaxDnsServers) return false;
    if (value.automatic) return value.servers.empty();
    if (value.servers.empty()) return false;
    return std::none_of(value.servers.begin(), value.servers.end(),
                        [](const Ipv4Address& server) { return server.isUnspecified(); });
}

bool validTcpSetting(const TcpSetting& setting) noexcept {
    return setting.parameter == TcpParameter::InitialRetransmissionTimeout &&
           setting.existed && setting.value >= 3000 && setting.value <= 65535;
}

bool validTcp(const TcpValue& value) noexcept {
    if (value.settings.size() != 1) return false;
    std::set<TcpParameter> seen;
    for (const auto& setting : value.settings) {
        if (!validTcpSetting(setting) || !seen.insert(setting.parameter).second) return false;
    }
    return true;
}

bool validPowerPlan(const PowerPlanValue& value) noexcept {
    if (!isGuid(value.identifier, false)) return false;
    std::string normalized = value.identifier;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    static constexpr std::array<std::string_view, 3> allowed{
        "381b4222-f694-41f0-9685-ff5bb260df2e",
        "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c",
        "e9a42b02-d5df-448d-aa00-03f14749eb61"};
    return std::find(allowed.begin(), allowed.end(), normalized) != allowed.end();
}

bool validDisabledRegistry(const RegistryValue& value) noexcept {
    const auto* scalar = std::get_if<uint32_t>(&value.value);
    return value.existed && scalar && *scalar == 0;
}

bool validGameDvr(const GameDvrValue& value) noexcept {
    return validDisabledRegistry(value.game_dvr_enabled) &&
           validDisabledRegistry(value.app_capture_enabled);
}

bool validFullscreen(const FullscreenValue& value) noexcept {
    return value.existed &&
           value.compatibility_flags == "~ DISABLEDXMAXIMIZEDWINDOWEDMODE";
}

bool validProposed(ActionId id, const ActionValue& value) noexcept {
    switch (id) {
    case ActionId::Dns: {
        const auto* typed = std::get_if<DnsValue>(&value);
        return typed && validDns(*typed);
    }
    case ActionId::Mtu: {
        const auto* typed = std::get_if<MtuValue>(&value);
        return typed && typed->bytes >= 576 && typed->bytes <= 9000;
    }
    case ActionId::TcpParameters: {
        const auto* typed = std::get_if<TcpValue>(&value);
        return typed && validTcp(*typed);
    }
    case ActionId::PowerPlan: {
        const auto* typed = std::get_if<PowerPlanValue>(&value);
        return typed && validPowerPlan(*typed);
    }
    case ActionId::GameDvr: {
        const auto* typed = std::get_if<GameDvrValue>(&value);
        return typed && validGameDvr(*typed);
    }
    case ActionId::FullscreenOptimizations: {
        const auto* typed = std::get_if<FullscreenValue>(&value);
        return typed && validFullscreen(*typed);
    }
    case ActionId::ProcessPriority: {
        const auto* typed = std::get_if<PriorityValue>(&value);
        return typed && *typed != PriorityValue::Realtime;
    }
    case ActionId::EnergyMode:
        return false;
    }
    return false;
}

AllowedRegistryKey registryKey(TcpParameter parameter) {
    switch (parameter) {
    case TcpParameter::AutoTuningLevel:
    case TcpParameter::CongestionProvider:
        break;
    case TcpParameter::InitialRetransmissionTimeout:
        return AllowedRegistryKey::TcpInitialRetransmissionTimeout;
    }
    return static_cast<AllowedRegistryKey>(-1);
}

Result<int32_t> registryInteger(const RegistryValue& value) {
    if (!value.existed) return success(int32_t{0});
    if (const auto* unsigned_value = std::get_if<uint32_t>(&value.value)) {
        if (*unsigned_value <= static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
            return success(static_cast<int32_t>(*unsigned_value));
        }
    }
    if (const auto* signed_value = std::get_if<int64_t>(&value.value)) {
        if (*signed_value >= std::numeric_limits<int32_t>::min() &&
            *signed_value <= std::numeric_limits<int32_t>::max()) {
            return success(static_cast<int32_t>(*signed_value));
        }
    }
    return failure<int32_t>(RemediationError::InternalFailure,
                            "allowlisted registry value has an invalid type");
}

class WindowsFixAction final : public FixAction {
public:
    WindowsFixAction(ActionId id, std::shared_ptr<WindowsStateApi> api,
                     ActionValue proposed)
        : id_(id), api_(std::move(api)), proposed_(std::move(proposed)) {}

    ActionId id() const noexcept override { return id_; }

    Result<ActionState> observe(
        const ActionTarget& target, const CancellationToken& cancellation) const override {
        ActionState state;
        state.id = id_;
        if (cancellation.isCancelled()) {
            return failure<ActionState>(RemediationError::Cancelled, "observation cancelled", state);
        }
        if (!api_ || !validTarget(id_, target)) {
            return failure<ActionState>(RemediationError::InvalidTarget, "invalid Windows action target", state);
        }

        const auto observed = readValue(target);
        if (!observed.ok()) {
            state.status = observed.error == RemediationError::Unsupported
                               ? ActionStatus::Unsupported
                               : ActionStatus::Failed;
            state.detail = observed.detail;
            return failure<ActionState>(observed.error, observed.detail, std::move(state));
        }
        state.value = observed.value;
        state.status = state.value == proposed_ ? ActionStatus::AlreadyConfigured
                                                : ActionStatus::Recommended;
        return success(std::move(state));
    }

    Result<PreparedAction> prepare(
        const ActionTarget& target, const ActionState& current) const override {
        if (!api_ || !validTarget(id_, target) || current.id != id_ ||
            !validProposed(id_, proposed_) || !matchesObservedType(current.value)) {
            return failure<PreparedAction>(RemediationError::InvalidTarget,
                                           "invalid Windows action target or value");
        }
        PreparedAction prepared;
        prepared.id = id_;
        prepared.target = target;
        prepared.before = current;
        prepared.proposed = {id_, ActionStatus::AlreadyConfigured, proposed_, {}};
        prepared.rollback_supported = true;
        return success(std::move(prepared));
    }

    Result<ActionState> apply(
        const PreparedAction& prepared, const CancellationToken& cancellation) override {
        return transition(prepared, cancellation, false);
    }

    Result<ActionState> rollback(
        const PreparedAction& prepared, const CancellationToken& cancellation) override {
        return transition(prepared, cancellation, true);
    }

private:
    Result<ActionValue> readValue(const ActionTarget& target) const {
        switch (id_) {
        case ActionId::Dns: return lift(api_->getDns(std::get<InterfaceId>(target)));
        case ActionId::Mtu: return lift(api_->getMtu(std::get<InterfaceId>(target)));
        case ActionId::PowerPlan: return lift(api_->getPowerPlan());
        case ActionId::GameDvr: {
            const auto game_dvr = api_->getAllowedRegistry(
                AllowedRegistryKey::GameDvrEnabled);
            if (!game_dvr.ok()) {
                return failure<ActionValue>(game_dvr.error, game_dvr.detail);
            }
            const auto app_capture = api_->getAllowedRegistry(
                AllowedRegistryKey::AppCaptureEnabled);
            if (!app_capture.ok()) {
                return failure<ActionValue>(app_capture.error, app_capture.detail);
            }
            return success(ActionValue{GameDvrValue{game_dvr.value,
                                                    app_capture.value}});
        }
        case ActionId::FullscreenOptimizations:
            return lift(api_->getFullscreenOptimizations(
                std::get<ExecutableIdentity>(target)));
        case ActionId::ProcessPriority:
            return lift(api_->getPriority(std::get<ProcessIdentity>(target)));
        case ActionId::TcpParameters: {
            TcpValue value;
            for (const auto parameter : {
                     TcpParameter::InitialRetransmissionTimeout}) {
                const auto registry = api_->getAllowedRegistry(registryKey(parameter));
                if (!registry.ok()) {
                    return failure<ActionValue>(registry.error, registry.detail);
                }
                const auto integer = registryInteger(registry.value);
                if (!integer.ok()) return failure<ActionValue>(integer.error, integer.detail);
                value.settings.push_back({parameter, registry.value.existed, integer.value});
            }
            return success(ActionValue{std::move(value)});
        }
        case ActionId::EnergyMode:
            break;
        }
        return failure<ActionValue>(RemediationError::Unsupported,
                                    "unsupported Windows action");
    }

    Result<std::monostate> writeValue(
        const ActionTarget& target, const ActionValue& value,
        const ActionValue& rollback_value) {
        switch (id_) {
        case ActionId::Dns:
            return api_->setDns(std::get<InterfaceId>(target), std::get<DnsValue>(value));
        case ActionId::Mtu:
            return api_->setMtu(std::get<InterfaceId>(target), std::get<MtuValue>(value));
        case ActionId::PowerPlan:
            return api_->setPowerPlan(std::get<PowerPlanValue>(value));
        case ActionId::GameDvr: {
            const auto& desired = std::get<GameDvrValue>(value);
            const auto& original = std::get<GameDvrValue>(rollback_value);
            const auto first = api_->setAllowedRegistry(
                AllowedRegistryKey::GameDvrEnabled,
                desired.game_dvr_enabled);
            if (!first.ok()) return first;
            const auto second = api_->setAllowedRegistry(
                AllowedRegistryKey::AppCaptureEnabled,
                desired.app_capture_enabled);
            if (second.ok()) return second;
            const auto compensated = api_->setAllowedRegistry(
                AllowedRegistryKey::GameDvrEnabled,
                original.game_dvr_enabled);
            if (!compensated.ok()) {
                return failure<std::monostate>(
                    RemediationError::RollbackFailed,
                    "Game DVR write failed and its partial prefix could not be restored");
            }
            return second;
        }
        case ActionId::FullscreenOptimizations:
            return api_->setFullscreenOptimizations(
                std::get<ExecutableIdentity>(target), std::get<FullscreenValue>(value));
        case ActionId::ProcessPriority:
            return api_->setPriority(std::get<ProcessIdentity>(target),
                                     std::get<PriorityValue>(value));
        case ActionId::TcpParameters: {
            const auto& settings = std::get<TcpValue>(value).settings;
            const auto& originals = std::get<TcpValue>(rollback_value).settings;
            std::size_t written_count = 0;
            for (const auto& setting : settings) {
                RegistryValue registry;
                registry.existed = setting.existed;
                if (setting.existed) registry.value = int64_t{setting.value};
                const auto result = api_->setAllowedRegistry(
                    registryKey(setting.parameter), registry);
                if (!result.ok()) {
                    bool compensation_failed = false;
                    while (written_count > 0) {
                        --written_count;
                        const auto& written = settings[written_count];
                        const auto original = std::find_if(
                            originals.begin(), originals.end(),
                            [&written](const TcpSetting& candidate) {
                                return candidate.parameter == written.parameter;
                            });
                        if (original == originals.end()) {
                            compensation_failed = true;
                            continue;
                        }
                        RegistryValue restore;
                        restore.existed = original->existed;
                        if (original->existed) restore.value = int64_t{original->value};
                        if (!api_->setAllowedRegistry(
                                registryKey(original->parameter), restore).ok()) {
                            compensation_failed = true;
                        }
                    }
                    if (compensation_failed) {
                        return failure<std::monostate>(
                            RemediationError::RollbackFailed,
                            "TCP write failed and its partial prefix could not be restored");
                    }
                    return result;
                }
                ++written_count;
            }
            return success(std::monostate{});
        }
        case ActionId::EnergyMode:
            break;
        }
        return failure<std::monostate>(RemediationError::Unsupported,
                                       "unsupported Windows action");
    }

    Result<ActionState> transition(
        const PreparedAction& prepared, const CancellationToken& cancellation,
        bool restoring) {
        if (cancellation.isCancelled()) {
            return failure<ActionState>(RemediationError::Cancelled,
                                        restoring ? "rollback cancelled" : "apply cancelled");
        }
        if (prepared.id != id_ || !validTarget(id_, prepared.target) ||
            prepared.before.id != id_ || prepared.proposed.id != id_ ||
            !(prepared.proposed.value == proposed_) ||
            !matchesObservedType(prepared.before.value)) {
            return failure<ActionState>(RemediationError::InvalidTarget,
                                        "invalid prepared Windows action");
        }

        const ActionValue& expected_current = restoring ? prepared.proposed.value
                                                        : prepared.before.value;
        const ActionValue& desired = restoring ? prepared.before.value
                                               : prepared.proposed.value;
        const auto fresh = readValue(prepared.target);
        if (!fresh.ok()) return failure<ActionState>(fresh.error, fresh.detail);
        if (!(fresh.value == expected_current)) {
            return failure<ActionState>(restoring ? RemediationError::RollbackFailed
                                                  : RemediationError::PreflightFailed,
                                        "Windows action state changed before mutation");
        }

        const auto written = writeValue(prepared.target, desired, expected_current);
        if (!written.ok()) {
            const auto after_failure = readValue(prepared.target);
            if (!after_failure.ok()) {
                return failure<ActionState>(RemediationError::RollbackFailed,
                                            "mutation failed and resulting state could not be observed");
            }
            if (!(after_failure.value == expected_current)) {
                const auto compensated = writeValue(
                    prepared.target, expected_current, after_failure.value);
                const auto restored = readValue(prepared.target);
                if (!compensated.ok() || !restored.ok() ||
                    !(restored.value == expected_current)) {
                    return failure<ActionState>(
                        RemediationError::RollbackFailed,
                        "mutation failed and exact before-state could not be restored");
                }
            }
            return failure<ActionState>(written.error, written.detail);
        }
        const auto verified = readValue(prepared.target);
        if (!verified.ok() || !(verified.value == desired)) {
            const auto compensated = writeValue(
                prepared.target, expected_current, desired);
            const auto restored = readValue(prepared.target);
            if (!compensated.ok() || !restored.ok() ||
                !(restored.value == expected_current)) {
                return failure<ActionState>(
                    RemediationError::RollbackFailed,
                    "post-write verification failed and exact before-state could not be restored");
            }
            return failure<ActionState>(
                verified.ok() ? RemediationError::VerificationMismatch : verified.error,
                verified.ok() ? "Windows action verification mismatch" : verified.detail);
        }

        ActionState state{id_, restoring ? ActionStatus::Reverted : ActionStatus::Applied,
                          verified.value, {}};
        return success(std::move(state));
    }

    bool matchesObservedType(const ActionValue& value) const noexcept {
        switch (id_) {
        case ActionId::Dns: return std::holds_alternative<DnsValue>(value);
        case ActionId::Mtu: return std::holds_alternative<MtuValue>(value);
        case ActionId::TcpParameters: return std::holds_alternative<TcpValue>(value);
        case ActionId::PowerPlan: return std::holds_alternative<PowerPlanValue>(value);
        case ActionId::GameDvr: return std::holds_alternative<GameDvrValue>(value);
        case ActionId::FullscreenOptimizations:
            return std::holds_alternative<FullscreenValue>(value);
        case ActionId::ProcessPriority:
            return std::holds_alternative<PriorityValue>(value);
        case ActionId::EnergyMode: return false;
        }
        return false;
    }

    template <typename T>
    static Result<ActionValue> lift(Result<T> result) {
        if (!result.ok()) return failure<ActionValue>(result.error, std::move(result.detail));
        return success(ActionValue{std::move(result.value)});
    }

    ActionId id_;
    std::shared_ptr<WindowsStateApi> api_;
    ActionValue proposed_;
};

std::unique_ptr<FixAction> makeAction(
    ActionId id, std::shared_ptr<WindowsStateApi> api, ActionValue proposed) {
    return std::make_unique<WindowsFixAction>(id, std::move(api), std::move(proposed));
}

} // namespace

std::unique_ptr<FixAction> makeDnsAction(
    std::shared_ptr<WindowsStateApi> api, DnsValue proposed) {
    return makeAction(ActionId::Dns, std::move(api), std::move(proposed));
}

std::unique_ptr<FixAction> makeMtuAction(
    std::shared_ptr<WindowsStateApi> api, MtuValue proposed) {
    return makeAction(ActionId::Mtu, std::move(api), proposed);
}

std::unique_ptr<FixAction> makeTcpParametersAction(
    std::shared_ptr<WindowsStateApi> api, TcpValue proposed) {
    return makeAction(ActionId::TcpParameters, std::move(api), std::move(proposed));
}

std::unique_ptr<FixAction> makePowerPlanAction(
    std::shared_ptr<WindowsStateApi> api, PowerPlanValue proposed) {
    return makeAction(ActionId::PowerPlan, std::move(api), std::move(proposed));
}

std::unique_ptr<FixAction> makeGameDvrAction(
    std::shared_ptr<WindowsStateApi> api, GameDvrValue proposed) {
    return makeAction(ActionId::GameDvr, std::move(api), std::move(proposed));
}

std::unique_ptr<FixAction> makeFullscreenOptimizationsAction(
    std::shared_ptr<WindowsStateApi> api, FullscreenValue proposed) {
    return makeAction(ActionId::FullscreenOptimizations, std::move(api),
                      std::move(proposed));
}

std::unique_ptr<FixAction> makePriorityAction(
    std::shared_ptr<WindowsStateApi> api, PriorityValue proposed) {
    return makeAction(ActionId::ProcessPriority, std::move(api), proposed);
}

std::vector<std::unique_ptr<FixAction>> createWindowsFixActions(
    std::shared_ptr<WindowsStateApi> api) {
    const auto first = Ipv4Address::parse("1.1.1.1");
    const auto second = Ipv4Address::parse("1.0.0.1");
    DnsValue dns{false, {}};
    if (first && second) dns.servers = {*first, *second};

    std::vector<std::unique_ptr<FixAction>> actions;
    actions.push_back(makePowerPlanAction(api));
    actions.push_back(makeGameDvrAction(api));
    actions.push_back(makeFullscreenOptimizationsAction(api));
    actions.push_back(makeTcpParametersAction(api, {{
        {TcpParameter::InitialRetransmissionTimeout, true, 5000}}}));
    actions.push_back(makeDnsAction(api, std::move(dns)));
    actions.push_back(makeMtuAction(api));
    actions.push_back(makePriorityAction(api));
    return actions;
}

} // namespace gno
