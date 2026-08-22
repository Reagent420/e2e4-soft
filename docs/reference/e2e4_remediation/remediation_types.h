#pragma once

#include "diagnostics/diagnostic_types.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace gno {

inline constexpr std::size_t kMaxTransactionActions = 8;
inline constexpr std::size_t kMaxTransactionIdLength = 64;
inline constexpr std::size_t kMaxInterfaceIdLength = 128;
inline constexpr std::size_t kMaxExecutablePathLength = 4096;
inline constexpr std::size_t kMaxDetailLength = 1024;
inline constexpr std::size_t kMaxPowerPlanIdLength = 128;
inline constexpr std::size_t kMaxRegistryStringLength = 4096;
inline constexpr std::size_t kMaxDnsServers = 8;
inline constexpr std::size_t kMaxTcpSettings = 16;
inline constexpr int kMinNiceValue = -20;
inline constexpr int kMaxNiceValue = 19;

enum class ActionId {
    PowerPlan,
    EnergyMode,
    GameDvr,
    FullscreenOptimizations,
    TcpParameters,
    Dns,
    Mtu,
    ProcessPriority
};

enum class ActionStatus {
    NotChecked,
    Recommended,
    AlreadyConfigured,
    Applied,
    Failed,
    Unsupported,
    Reverted
};

enum class RemediationError {
    None,
    Unsupported,
    InvalidTarget,
    PermissionDenied,
    ElevationCancelled,
    PreflightFailed,
    BackupFailed,
    ApplyFailed,
    VerificationMismatch,
    Timeout,
    Cancelled,
    RollbackFailed,
    Busy,
    InternalFailure
};

template <typename T>
struct Result {
    T value{};
    RemediationError error = RemediationError::InternalFailure;
    std::string detail;

    bool ok() const noexcept { return error == RemediationError::None; }
};

struct InterfaceId {
    std::string value;
    uint64_t luid = 0;
};

inline bool operator==(const InterfaceId& left, const InterfaceId& right) noexcept {
    return left.value == right.value && left.luid == right.luid;
}

inline bool operator!=(const InterfaceId& left, const InterfaceId& right) noexcept {
    return !(left == right);
}

struct ExecutableIdentity {
    std::string canonical_path;
};

inline bool operator==(
    const ExecutableIdentity& left, const ExecutableIdentity& right) noexcept {
    return left.canonical_path == right.canonical_path;
}

inline bool operator!=(
    const ExecutableIdentity& left, const ExecutableIdentity& right) noexcept {
    return !(left == right);
}

struct ProcessIdentity {
    uint32_t pid = 0;
    uint64_t creation_time = 0;
    std::string executable_path;
};

inline bool operator==(
    const ProcessIdentity& left, const ProcessIdentity& right) noexcept {
    return left.pid == right.pid && left.creation_time == right.creation_time &&
           left.executable_path == right.executable_path;
}

inline bool operator!=(
    const ProcessIdentity& left, const ProcessIdentity& right) noexcept {
    return !(left == right);
}

using ActionTarget =
    std::variant<std::monostate, InterfaceId, ExecutableIdentity, ProcessIdentity>;

struct DnsValue {
    bool automatic = true;
    std::vector<Ipv4Address> servers;
};

inline bool operator==(const DnsValue& left, const DnsValue& right) noexcept {
    return left.automatic == right.automatic && left.servers == right.servers;
}

struct MtuValue {
    uint32_t bytes = 0;
};

inline bool operator==(const MtuValue& left, const MtuValue& right) noexcept {
    return left.bytes == right.bytes;
}

enum class TcpParameter {
    AutoTuningLevel,
    CongestionProvider,
    InitialRetransmissionTimeout
};

struct TcpSetting {
    TcpParameter parameter = TcpParameter::AutoTuningLevel;
    bool existed = false;
    int32_t value = 0;
};

inline bool operator==(const TcpSetting& left, const TcpSetting& right) noexcept {
    return left.parameter == right.parameter && left.existed == right.existed &&
           left.value == right.value;
}

struct TcpValue {
    std::vector<TcpSetting> settings;
};

inline bool operator==(const TcpValue& left, const TcpValue& right) noexcept {
    return left.settings == right.settings;
}

struct PowerPlanValue {
    std::string identifier;
};

inline bool operator==(
    const PowerPlanValue& left, const PowerPlanValue& right) noexcept {
    return left.identifier == right.identifier;
}

enum class EnergyMode { Automatic, LowPower, Balanced, HighPower };

struct EnergyValue {
    EnergyMode mode = EnergyMode::Automatic;
};

inline bool operator==(const EnergyValue& left, const EnergyValue& right) noexcept {
    return left.mode == right.mode;
}

using RegistryScalar = std::variant<std::monostate, uint32_t, int64_t, std::string>;

struct RegistryValue {
    bool existed = false;
    RegistryScalar value;
    bool key_existed = true;
};

inline bool operator==(
    const RegistryValue& left, const RegistryValue& right) noexcept {
    return left.existed == right.existed && left.value == right.value &&
           left.key_existed == right.key_existed;
}

struct GameDvrValue {
    RegistryValue game_dvr_enabled;
    RegistryValue app_capture_enabled;
};

inline bool operator==(
    const GameDvrValue& left, const GameDvrValue& right) noexcept {
    return left.game_dvr_enabled == right.game_dvr_enabled &&
           left.app_capture_enabled == right.app_capture_enabled;
}

struct FullscreenValue {
    bool existed = false;
    std::string compatibility_flags;
    bool key_existed = true;
};

inline bool operator==(
    const FullscreenValue& left, const FullscreenValue& right) noexcept {
    return left.existed == right.existed &&
           left.compatibility_flags == right.compatibility_flags &&
           left.key_existed == right.key_existed;
}

enum class PriorityValue { Idle, BelowNormal, Normal, AboveNormal, High, Realtime };

struct NiceValue {
    int value = 0;
};

inline bool operator==(const NiceValue& left, const NiceValue& right) noexcept {
    return left.value == right.value;
}

using ActionValue = std::variant<std::monostate, DnsValue, MtuValue, TcpValue,
                                 PowerPlanValue, EnergyValue, RegistryValue,
                                 GameDvrValue, FullscreenValue, PriorityValue,
                                 NiceValue>;

struct ActionState {
    ActionId id = ActionId::PowerPlan;
    ActionStatus status = ActionStatus::NotChecked;
    ActionValue value;
    std::string detail;
};

inline bool operator==(const ActionState& left, const ActionState& right) noexcept {
    return left.id == right.id && left.status == right.status &&
           left.value == right.value && left.detail == right.detail;
}

inline bool operator!=(const ActionState& left, const ActionState& right) noexcept {
    return !(left == right);
}

struct PreparedAction {
    ActionId id = ActionId::PowerPlan;
    ActionTarget target;
    ActionState before;
    ActionState proposed;
    bool rollback_supported = false;
};

struct ActionOutcome {
    ActionId id = ActionId::PowerPlan;
    ActionStatus status = ActionStatus::NotChecked;
    RemediationError error = RemediationError::None;
    bool attempted = false;
    ActionState state;
    std::string detail;
    RemediationError rollback_error = RemediationError::None;
    bool rollback_attempted = false;
    std::string rollback_detail;
};

enum class TransactionStatus {
    Unprepared,
    Prepared,
    Applying,
    Applied,
    Failed,
    RollingBack,
    Reverted,
    RollbackFailed,
    Cancelled
};

struct TransactionRecord {
    uint32_t schema_version = 1;
    std::string transaction_id;
    TransactionStatus status = TransactionStatus::Unprepared;
    std::vector<PreparedAction> prepared_actions;
    std::vector<ActionOutcome> outcomes;
    std::vector<ActionId> action_order;
    std::vector<ActionId> applied_action_order;
    RemediationError error = RemediationError::None;
    std::string detail;
};

struct TransactionSummary {
    std::string transaction_id;
    TransactionStatus status = TransactionStatus::Unprepared;
};

inline bool isBounded(const ActionTarget& target) noexcept {
    return std::visit(
        [](const auto& value) noexcept {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (std::is_same<Value, InterfaceId>::value) {
                return !value.value.empty() && value.value.size() <= kMaxInterfaceIdLength;
            } else if constexpr (std::is_same<Value, ExecutableIdentity>::value) {
                return !value.canonical_path.empty() &&
                       value.canonical_path.size() <= kMaxExecutablePathLength;
            } else if constexpr (std::is_same<Value, ProcessIdentity>::value) {
                return value.pid != 0 && value.creation_time != 0 &&
                       !value.executable_path.empty() &&
                       value.executable_path.size() <= kMaxExecutablePathLength;
            } else {
                return true;
            }
        },
        target);
}

inline bool isBounded(const ActionValue& action_value) noexcept {
    return std::visit(
        [](const auto& value) noexcept {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (std::is_same<Value, DnsValue>::value) {
                return value.servers.size() <= kMaxDnsServers;
            } else if constexpr (std::is_same<Value, TcpValue>::value) {
                return value.settings.size() <= kMaxTcpSettings;
            } else if constexpr (std::is_same<Value, PowerPlanValue>::value) {
                return !value.identifier.empty() &&
                       value.identifier.size() <= kMaxPowerPlanIdLength;
            } else if constexpr (std::is_same<Value, RegistryValue>::value) {
                if (const auto* text = std::get_if<std::string>(&value.value)) {
                    return text->size() <= kMaxRegistryStringLength;
                }
                return true;
            } else if constexpr (std::is_same<Value, GameDvrValue>::value) {
                const auto bounded_registry = [](const RegistryValue& registry) {
                    if (const auto* text = std::get_if<std::string>(&registry.value)) {
                        return text->size() <= kMaxRegistryStringLength;
                    }
                    return true;
                };
                return bounded_registry(value.game_dvr_enabled) &&
                       bounded_registry(value.app_capture_enabled);
            } else if constexpr (std::is_same<Value, FullscreenValue>::value) {
                return value.compatibility_flags.size() <= kMaxRegistryStringLength;
            } else {
                return true;
            }
        },
        action_value);
}

inline bool isSafeProposed(const ActionValue& action_value) noexcept {
    if (!isBounded(action_value)) {
        return false;
    }
    if (const auto* priority = std::get_if<PriorityValue>(&action_value)) {
        return *priority != PriorityValue::Realtime;
    }
    if (const auto* nice = std::get_if<NiceValue>(&action_value)) {
        return nice->value >= kMinNiceValue && nice->value <= kMaxNiceValue;
    }
    return true;
}

} // namespace gno
