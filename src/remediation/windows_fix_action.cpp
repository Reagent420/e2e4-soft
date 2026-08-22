#include "remediation/windows_fix_action.h"

#include <utility>

namespace gno {
namespace remediation {

namespace {

constexpr const char* kHighPerformancePlan = "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c";
constexpr const char* kDnsPrimary = "1.1.1.1";
constexpr const char* kDnsSecondary = "1.0.0.1";
constexpr std::uint32_t kDesiredMtu = 1500;
constexpr std::int32_t kDesiredInitialRtt = 5000;
constexpr const char* kFullscreenFlags = "~ DISABLEDXMAXIMIZEDWINDOWEDMODE";

ActionValue proposedValueFor(ActionId id) {
    switch (id) {
        case ActionId::PowerPlan:
            return PowerPlanValue{kHighPerformancePlan};
        case ActionId::GameDvr:
            return GameDvrValue{RegistryData{true, 0, true}, RegistryData{true, 0, true}};
        case ActionId::FullscreenOptimizations:
            return FullscreenValue{true, kFullscreenFlags, true};
        case ActionId::TcpParameters:
            return TcpValue{{TcpSetting{TcpParameter::InitialRetransmissionTimeout, true, kDesiredInitialRtt}}};
        case ActionId::Dns:
            return DnsValue{false, {kDnsPrimary, kDnsSecondary}};
        case ActionId::Mtu:
            return MtuValue{kDesiredMtu};
        case ActionId::ProcessPriority:
            return PriorityValue{PriorityLevel::AboveNormal};
        case ActionId::EnergyMode:
            break;
    }
    return NoneValue{};
}

bool valuesEqual(const ActionValue& a, const ActionValue& b) { return a == b; }

} // namespace

WindowsFixAction::WindowsFixAction(ActionId id, WindowsStateApi& api) : id_(id), api_(api) {}

Result<ActionState> WindowsFixAction::observe(const ActionTarget& target) {
    if (!isValidTarget(target))
        return Fail(RemediationError::InvalidTarget, "invalid target for " + to_string(id_));

    ActionValue current;
    switch (id_) {
        case ActionId::PowerPlan: {
            auto plan = api_.getPowerPlan();
            if (!plan) return Result<ActionState>(plan.error());
            current = plan.value();
            break;
        }
        case ActionId::GameDvr: {
            auto first = api_.getAllowedRegistry(AllowedRegistryKey::GameDvrEnabled);
            if (!first) return Result<ActionState>(first.error());
            auto second = api_.getAllowedRegistry(AllowedRegistryKey::AppCaptureEnabled);
            if (!second) return Result<ActionState>(second.error());
            current = GameDvrValue{first.value(), second.value()};
            break;
        }
        case ActionId::FullscreenOptimizations: {
            const auto& exe = std::get<ExecutableTarget>(target);
            auto state = api_.getFullscreenOptimizations(exe);
            if (!state) return Result<ActionState>(state.error());
            current = state.value();
            break;
        }
        case ActionId::TcpParameters: {
            auto data = api_.getAllowedRegistry(AllowedRegistryKey::TcpInitialRetransmissionTimeout);
            if (!data) return Result<ActionState>(data.error());
            current = TcpValue{std::vector<TcpSetting>{TcpSetting{
                    TcpParameter::InitialRetransmissionTimeout, data.value().existed,
                    static_cast<std::int32_t>(data.value().value)}}};
            break;
        }
        case ActionId::Dns: {
            const auto& iface = std::get<InterfaceTarget>(target);
            auto dns = api_.getDns(iface);
            if (!dns) return Result<ActionState>(dns.error());
            current = dns.value();
            break;
        }
        case ActionId::Mtu: {
            const auto& iface = std::get<InterfaceTarget>(target);
            auto mtu = api_.getMtu(iface);
            if (!mtu) return Result<ActionState>(mtu.error());
            current = mtu.value();
            break;
        }
        case ActionId::ProcessPriority: {
            const auto& process = std::get<ProcessTarget>(target);
            auto level = api_.getPriority(process);
            if (!level) return Result<ActionState>(level.error());
            current = PriorityValue{level.value()};
            break;
        }
        case ActionId::EnergyMode:
            return Fail(RemediationError::Unsupported, "EnergyMode is not implemented");
    }

    const ActionValue proposed = proposedValueFor(id_);
    ActionState state;
    state.id = id_;
    state.value = current;
    state.status = valuesEqual(current, proposed) ? ActionStatus::AlreadyConfigured
                                                  : ActionStatus::Recommended;
    state.detail.clear();
    return state;
}

Result<PreparedAction> WindowsFixAction::prepare(const ActionTarget& target, const ActionState& current) {
    if (current.id != id_)
        return Fail(RemediationError::InvalidTarget, "state id mismatch");
    if (!isValidTarget(target))
        return Fail(RemediationError::InvalidTarget, "invalid target for " + to_string(id_));
    if (!isValidState(current))
        return Fail(RemediationError::PreflightFailed, "current state out of bounds");

    PreparedAction prepared;
    prepared.id = id_;
    prepared.target = target;
    prepared.before = current;

    prepared.proposed.id = id_;
    prepared.proposed.status = ActionStatus::AlreadyConfigured;
    prepared.proposed.value = proposedValueFor(id_);
    prepared.proposed.detail.clear();

    switch (id_) {
        case ActionId::PowerPlan:
        case ActionId::GameDvr:
        case ActionId::FullscreenOptimizations:
        case ActionId::TcpParameters:
        case ActionId::Dns:
        case ActionId::Mtu:
        case ActionId::ProcessPriority:
            prepared.safe = true;
            break;
        default:
            prepared.safe = false;
    }
    return prepared;
}

Result<ActionState> WindowsFixAction::apply(const PreparedAction& prepared) {
    return transition(prepared, false);
}

Result<ActionState> WindowsFixAction::rollback(const PreparedAction& prepared) {
    return transition(prepared, true);
}

Result<ActionState> WindowsFixAction::transition(const PreparedAction& prepared, bool restoring) {
    const ActionValue desired = restoring ? prepared.before.value : prepared.proposed.value;
    const ActionValue expected_current = restoring ? prepared.proposed.value : prepared.before.value;

    // 1. Read live value and make sure it still matches what we saw at prepare time.
    Result<ActionValue> fresh{NoneValue{}};
    switch (id_) {
        case ActionId::PowerPlan: {
            auto r = api_.getPowerPlan();
            if (!r) return Result<ActionState>(r.error());
            fresh = Result<ActionValue>(r.value());
            break;
        }
        case ActionId::GameDvr: {
            auto a = api_.getAllowedRegistry(AllowedRegistryKey::GameDvrEnabled);
            if (!a) return Result<ActionState>(a.error());
            auto b = api_.getAllowedRegistry(AllowedRegistryKey::AppCaptureEnabled);
            if (!b) return Result<ActionState>(b.error());
            fresh = Result<ActionValue>(GameDvrValue{a.value(), b.value()});
            break;
        }
        case ActionId::FullscreenOptimizations: {
            auto r = api_.getFullscreenOptimizations(std::get<ExecutableTarget>(prepared.target));
            if (!r) return Result<ActionState>(r.error());
            fresh = Result<ActionValue>(r.value());
            break;
        }
        case ActionId::TcpParameters: {
            auto r = api_.getAllowedRegistry(AllowedRegistryKey::TcpInitialRetransmissionTimeout);
            if (!r) return Result<ActionState>(r.error());
            fresh = Result<ActionValue>(
                TcpValue{std::vector<TcpSetting>{TcpSetting{
                        TcpParameter::InitialRetransmissionTimeout, r.value().existed,
                        static_cast<std::int32_t>(r.value().value)}}});
            break;
        }
        case ActionId::Dns: {
            auto r = api_.getDns(std::get<InterfaceTarget>(prepared.target));
            if (!r) return Result<ActionState>(r.error());
            fresh = Result<ActionValue>(r.value());
            break;
        }
        case ActionId::Mtu: {
            auto r = api_.getMtu(std::get<InterfaceTarget>(prepared.target));
            if (!r) return Result<ActionState>(r.error());
            fresh = Result<ActionValue>(r.value());
            break;
        }
        case ActionId::ProcessPriority: {
            auto r = api_.getPriority(std::get<ProcessTarget>(prepared.target));
            if (!r) return Result<ActionState>(r.error());
            fresh = Result<ActionValue>(PriorityValue{r.value()});
            break;
        }
        default:
            return Fail(RemediationError::Unsupported, "unsupported action");
    }

    if (!valuesEqual(fresh.value(), expected_current))
        return Fail(RemediationError::PreflightFailed, "state changed before write");

    // 2. Write the desired value.
    SimpleResult written = Ok();
    switch (id_) {
        case ActionId::PowerPlan:
            written = api_.setPowerPlan(std::get<PowerPlanValue>(desired));
            break;
        case ActionId::GameDvr: {
            const auto& dvr = std::get<GameDvrValue>(desired);
            written = api_.setAllowedRegistry(AllowedRegistryKey::GameDvrEnabled, dvr.game_dvr_enabled);
            if (written)
                written = api_.setAllowedRegistry(AllowedRegistryKey::AppCaptureEnabled, dvr.app_capture_enabled);
            break;
        }
        case ActionId::FullscreenOptimizations:
            written = api_.setFullscreenOptimizations(std::get<ExecutableTarget>(prepared.target),
                                                      std::get<FullscreenValue>(desired));
            break;
        case ActionId::TcpParameters: {
            const auto& tcp = std::get<TcpValue>(desired);
            RegistryData data{false, 0, false};
            if (!tcp.settings.empty()) {
                data.existed = tcp.settings[0].existed;
                data.value = static_cast<std::uint32_t>(tcp.settings[0].value);
                data.key_existed = tcp.settings[0].existed;
            }
            written = api_.setAllowedRegistry(AllowedRegistryKey::TcpInitialRetransmissionTimeout, data);
            break;
        }
        case ActionId::Dns:
            written = api_.setDns(std::get<InterfaceTarget>(prepared.target), std::get<DnsValue>(desired));
            break;
        case ActionId::Mtu:
            written = api_.setMtu(std::get<InterfaceTarget>(prepared.target), std::get<MtuValue>(desired));
            break;
        case ActionId::ProcessPriority:
            written = api_.setPriority(std::get<ProcessTarget>(prepared.target),
                                       std::get<PriorityValue>(desired).level);
            break;
        default:
            return Fail(RemediationError::Unsupported, "unsupported action");
    }
    if (!written) return Result<ActionState>(written.error());

    // 3. Verify by reading back.
    Result<ActionState> verified_state = observe(prepared.target);
    if (!verified_state) return verified_state;

    bool matches_desired = false;
    std::visit([&](const auto& v) {
        using U = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<U, DnsValue>) matches_desired = v == std::get<DnsValue>(desired);
        else if constexpr (std::is_same_v<U, MtuValue>) matches_desired = v == std::get<MtuValue>(desired);
        else if constexpr (std::is_same_v<U, TcpValue>) matches_desired = v == std::get<TcpValue>(desired);
        else if constexpr (std::is_same_v<U, PowerPlanValue>) matches_desired = v == std::get<PowerPlanValue>(desired);
        else if constexpr (std::is_same_v<U, GameDvrValue>) matches_desired = v == std::get<GameDvrValue>(desired);
        else if constexpr (std::is_same_v<U, FullscreenValue>) matches_desired = v == std::get<FullscreenValue>(desired);
        else if constexpr (std::is_same_v<U, PriorityValue>) matches_desired = v == std::get<PriorityValue>(desired);
        else matches_desired = true;
    }, verified_state.value().value);

    if (!matches_desired)
        return Fail(RemediationError::VerificationMismatch, "post-write verification failed");

    return verified_state;
}

std::vector<std::unique_ptr<FixAction>> createActions(WindowsStateApi& api) {
    std::vector<std::unique_ptr<FixAction>> actions;
    actions.push_back(std::make_unique<WindowsFixAction>(ActionId::PowerPlan, api));
    actions.push_back(std::make_unique<WindowsFixAction>(ActionId::GameDvr, api));
    actions.push_back(std::make_unique<WindowsFixAction>(ActionId::FullscreenOptimizations, api));
    actions.push_back(std::make_unique<WindowsFixAction>(ActionId::TcpParameters, api));
    actions.push_back(std::make_unique<WindowsFixAction>(ActionId::Dns, api));
    actions.push_back(std::make_unique<WindowsFixAction>(ActionId::Mtu, api));
    actions.push_back(std::make_unique<WindowsFixAction>(ActionId::ProcessPriority, api));
    return actions;
}

} // namespace remediation
} // namespace gno
