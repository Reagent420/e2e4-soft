#pragma once

// Self-contained safe-remediation domain types (port of the C# Gno.Core.Remediation
// model, which itself mirrors the e2e4 fix_transaction design).
// No Qt, no external JSON library.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <variant>

namespace gno {
namespace remediation {

inline constexpr std::size_t kMaxTransactionActions = 8;
inline constexpr std::size_t kMaxTransactionIdLength = 64;
inline constexpr std::size_t kMaxInterfaceIdLength = 128;
inline constexpr std::size_t kMaxExecutablePathLength = 4096;
inline constexpr std::size_t kMaxDetailLength = 1024;
inline constexpr std::size_t kMaxDnsServers = 8;
inline constexpr std::size_t kMaxTcpSettings = 16;

enum class ActionId {
    PowerPlan = 0,
    EnergyMode = 1,
    GameDvr = 2,
    FullscreenOptimizations = 3,
    TcpParameters = 4,
    Dns = 5,
    Mtu = 6,
    ProcessPriority = 7,
    Cs2MaxPing = 8
};

enum class ActionStatus {
    NotChecked = 0,
    Recommended = 1,
    AlreadyConfigured = 2,
    Applied = 3,
    Failed = 4,
    Unsupported = 5,
    Reverted = 6
};

enum class RemediationError {
    None = 0,
    Unsupported = 1,
    InvalidTarget = 2,
    PermissionDenied = 3,
    ElevationCancelled = 4,
    PreflightFailed = 5,
    BackupFailed = 6,
    ApplyFailed = 7,
    VerificationMismatch = 8,
    Timeout = 9,
    Cancelled = 10,
    RollbackFailed = 11,
    Busy = 12,
    InternalFailure = 13
};

enum class TransactionStatus {
    Unprepared = 0,
    Prepared = 1,
    Applying = 2,
    Applied = 3,
    Failed = 4,
    RollingBack = 5,
    Reverted = 6,
    RollbackFailed = 7,
    Cancelled = 8
};

enum class TcpParameter {
    InitialRetransmissionTimeout = 0
};

enum class PriorityLevel {
    Normal = 0,
    AboveNormal = 1,
    High = 2
};

enum class AllowedRegistryKey {
    GameDvrEnabled = 0,
    AppCaptureEnabled = 1,
    TcpInitialRetransmissionTimeout = 2
};

// ---------------------------------------------------------------- Result<T>

struct Error {
    RemediationError code = RemediationError::None;
    std::string detail;

    static Error make(RemediationError c, std::string d = {}) { return Error{c, std::move(d)}; }
};

template <typename T>
class Result {
public:
    Result(T v) : value_(std::move(v)), error_{} {}
    Result(Error e) : value_{}, error_(std::move(e)) {}

    bool ok() const noexcept { return error_.code == RemediationError::None; }
    explicit operator bool() const noexcept { return ok(); }

    const T& value() const noexcept { return value_; }
    T& value() noexcept { return value_; }
    const Error& error() const noexcept { return error_; }
    RemediationError code() const noexcept { return error_.code; }
    const std::string& detail() const noexcept { return error_.detail; }

private:
    T value_;
    Error error_;
};

class Unit {
public:
    bool operator==(const Unit&) const { return true; }
};
using SimpleResult = Result<Unit>;

inline SimpleResult Ok() { return SimpleResult(Unit{}); }
inline Error Fail(RemediationError c, std::string d = {}) { return Error::make(c, std::move(d)); }

// ---------------------------------------------------------------- targets / values

struct NoTarget {
    bool operator==(const NoTarget&) const { return true; }
};
struct InterfaceTarget {
    std::string id;      // interface GUID (as in NetworkInterface.Id)
    std::uint64_t index = 0;
    bool operator==(const InterfaceTarget&) const = default;
};
struct ExecutableTarget {
    std::string path;
    bool operator==(const ExecutableTarget&) const = default;
};
struct ProcessTarget {
    std::uint32_t pid = 0;
    std::uint64_t creation_time = 0;
    std::string path;
    bool operator==(const ProcessTarget&) const = default;
};

using ActionTarget = std::variant<NoTarget, InterfaceTarget, ExecutableTarget, ProcessTarget>;

struct NoneValue {
    bool operator==(const NoneValue&) const { return true; }
};
struct RegistryData {
    bool existed = false;
    std::uint32_t value = 0;
    bool key_existed = false;
    bool operator==(const RegistryData&) const = default;
};
struct DnsValue {
    bool automatic = true;
    std::vector<std::string> servers;
    bool operator==(const DnsValue&) const = default;
};
struct MtuValue {
    std::uint32_t bytes = 1500;
    bool operator==(const MtuValue&) const = default;
};
struct TcpSetting {
    TcpParameter parameter = TcpParameter::InitialRetransmissionTimeout;
    bool existed = false;
    std::int32_t value = 0;
    bool operator==(const TcpSetting&) const = default;
};
struct TcpValue {
    std::vector<TcpSetting> settings;
    bool operator==(const TcpValue&) const = default;
};
struct PowerPlanValue {
    std::string identifier; // power scheme GUID
    bool operator==(const PowerPlanValue&) const = default;
};
struct GameDvrValue {
    RegistryData game_dvr_enabled;
    RegistryData app_capture_enabled;
    bool operator==(const GameDvrValue&) const = default;
};
struct FullscreenValue {
    bool existed = false;
    std::string compatibility_flags;
    bool key_existed = false;
    bool operator==(const FullscreenValue&) const = default;
};
struct PriorityValue {
    PriorityLevel level = PriorityLevel::Normal;
    bool operator==(const PriorityValue&) const = default;
};
struct Cs2MaxPingValue {
    std::uint32_t max_ping = 60;
    bool operator==(const Cs2MaxPingValue&) const = default;
};

using ActionValue = std::variant<NoneValue, DnsValue, MtuValue, TcpValue, PowerPlanValue,
                                 GameDvrValue, FullscreenValue, PriorityValue, Cs2MaxPingValue>;

// ---------------------------------------------------------------- records

struct ActionState {
    ActionId id = ActionId::PowerPlan;
    ActionStatus status = ActionStatus::NotChecked;
    ActionValue value;
    std::string detail;
    bool operator==(const ActionState&) const = default;
};

struct PreparedAction {
    ActionId id = ActionId::PowerPlan;
    ActionTarget target;
    ActionState before;
    ActionState proposed;
    bool safe = false;
    bool operator==(const PreparedAction&) const = default;
};

struct ActionOutcome {
    ActionId id = ActionId::PowerPlan;
    ActionStatus status = ActionStatus::NotChecked;
    RemediationError error = RemediationError::None;
    ActionState state;
    bool attempted = false;
    bool rollback_attempted = false;
    RemediationError rollback_error = RemediationError::None;
    std::string detail;
    std::string rollback_detail;

    bool operator==(const ActionOutcome&) const = default;
};

struct TransactionRecord {
    std::uint32_t schema_version = 1;
    std::string transaction_id;
    TransactionStatus status = TransactionStatus::Unprepared;
    std::vector<PreparedAction> prepared_actions;
    std::vector<ActionOutcome> outcomes;
    std::vector<ActionId> action_order;
    std::vector<ActionId> applied_action_order;
    RemediationError error = RemediationError::None;
    std::string detail;

    bool operator==(const TransactionRecord&) const = default;
};

struct TransactionSummary {
    std::string transaction_id;
    TransactionStatus status = TransactionStatus::Unprepared;
    std::string created_at;
};

// ---------------------------------------------------------------- validation

inline bool isValidTarget(const ActionTarget& target) {
    return std::visit([](const auto& t) -> bool {
        using U = std::decay_t<decltype(t)>;
        if constexpr (std::is_same_v<U, InterfaceTarget>) {
            return !t.id.empty() && t.id.size() <= kMaxInterfaceIdLength;
        } else if constexpr (std::is_same_v<U, ExecutableTarget>) {
            return !t.path.empty() && t.path.size() <= kMaxExecutablePathLength;
        } else if constexpr (std::is_same_v<U, ProcessTarget>) {
            return t.pid != 0 && t.creation_time != 0 && !t.path.empty() &&
                   t.path.size() <= kMaxExecutablePathLength;
        } else {
            return true;
        }
    }, target);
}

inline bool isValidValue(const ActionValue& value) {
    return std::visit([](const auto& v) -> bool {
        using U = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<U, DnsValue>) {
            return v.servers.size() <= kMaxDnsServers;
        } else if constexpr (std::is_same_v<U, TcpValue>) {
            return v.settings.size() <= kMaxTcpSettings;
        } else if constexpr (std::is_same_v<U, MtuValue>) {
            return v.bytes >= 576 && v.bytes <= 9000;
        } else if constexpr (std::is_same_v<U, PowerPlanValue>) {
            return !v.identifier.empty() && v.identifier.size() <= 128;
        } else if constexpr (std::is_same_v<U, Cs2MaxPingValue>) {
            return v.max_ping >= 20 && v.max_ping <= 350;
        } else {
            return true;
        }
    }, value);
}

inline bool isValidState(const ActionState& state) {
    return isValidValue(state.value) && state.detail.size() <= kMaxDetailLength;
}

inline std::string to_string(ActionId id) {
    switch (id) {
        case ActionId::PowerPlan: return "PowerPlan";
        case ActionId::EnergyMode: return "EnergyMode";
        case ActionId::GameDvr: return "GameDvr";
        case ActionId::FullscreenOptimizations: return "FullscreenOptimizations";
        case ActionId::TcpParameters: return "TcpParameters";
        case ActionId::Dns: return "Dns";
        case ActionId::Mtu: return "Mtu";
        case ActionId::ProcessPriority: return "ProcessPriority";
        case ActionId::Cs2MaxPing: return "Cs2MaxPing";
    }
    return "Unknown";
}

} // namespace remediation
} // namespace gno
