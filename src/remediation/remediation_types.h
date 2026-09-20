#pragma once

// Self-contained safe-remediation domain types (port of the C# Gno.Core.Remediation
// model, which itself mirrors the e2e4 fix_transaction design).
// No Qt, no external JSON library.

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>
#include <variant>

namespace gno {
namespace remediation {

inline constexpr std::size_t kMaxTransactionActions = 12;
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
    Cs2MaxPing = 8,
    GameMode = 9,
    MouseAccel = 10,
    Cs2FpsUnlock = 11
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
    TcpInitialRetransmissionTimeout = 2,
    GameModeAllow = 3
};

// ---------------------------------------------------------------- Result<T>
//
// ГРАНИЦА ПРИМЕНЕНИЯ result-типов в проекте (см. также diagnostic_types.h).
//
// В проекте два result-типа. Они намеренно не объединены в один шаблон:
// semantics у слоёв разные, и объединение сломало бы ~100 мест в исходниках
// и тестах, не дав ничего функционально нового.
//
//   remediation::Result<T>   — результат ЗАПИСИ в систему (транзакция).
//   DiagnosticResult<T>      — результат ЧТЕНИЯ/измерения (диагностика).
//
// Чем они отличаются и когда какой использовать:
//
//  1. Значение: у remediation-результента .value() — это МЕТОД, потому что
//     значение может существовать ОДНОВРЕМЕННО с ошибкой (конструктор
//     Result(T v, Error e)). Это нужно для частично применённой транзакции:
//     статус — ошибка (часть действий упала), но в outcome лежат результаты
//     по каждому действию, и без них UI не покажет, что именно применилось.
//     У DiagnosticResult значение и ошибка не встречаются вместе: измерение
//     либо прошло (есть value), либо нет (только error) — поэтому там
//     .value — обычное ПОЛЕ.
//
//  2. Ошибка: remediation несёт Error{code, detail} — код + человекочитаемое
//     пояснение, которое показывается пользователю и пишется в журнал.
//     DiagnosticResult хранит один DiagnosticError без поялашений: для
//     измерения кода достаточно, а текст собирается на уровне UI
//     (PlainLanguageReport::errorLabel).
//
//  3. Создание: remediation-результат строится через Ok()/Fail() или
//     конструктор от значения/ошибки; DiagnosticResult — агрегатной
//     инициализацией {value, error}.
//
// ПРАВИЛО: пишешь в систему (реестр, netsh, powercfg, приоритет процесса) —
// remediation::Result<T>. Измеряешь/наблюдаешь (пинг, соединения, маршруты) —
// DiagnosticResult<T>. Смешивать не нужно: если в диагностике появился
// результат с «частично применённым значением», это признак того, что
// диагностика начала писать в систему, а она не должна.

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
    // Значение вместе с ошибкой: нужно там, где неудача всё равно несёт
    // полезные данные. Например, частично применённая транзакция: это ошибка
    // (часть действий упала), но в outcome лежат результаты по каждому
    // действию, и без них UI не может показать, что именно применилось.
    Result(T v, Error e) : value_(std::move(v)), error_(std::move(e)) {}

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
struct Cs2FpsUnlockValue {
    std::uint32_t fps_cap = 0; // 0 = unlimited
    bool operator==(const Cs2FpsUnlockValue&) const = default;
};
struct MouseAccelValue {
    std::uint32_t speed = 0;      // 1 = enhanced pointer precision on
    std::uint32_t threshold1 = 0;
    std::uint32_t threshold2 = 0;
    bool operator==(const MouseAccelValue&) const = default;
};

using ActionValue = std::variant<NoneValue, DnsValue, MtuValue, TcpValue, PowerPlanValue,
                                 GameDvrValue, FullscreenValue, PriorityValue, Cs2MaxPingValue,
                                 MouseAccelValue, RegistryData>;

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
    // Момент СОЗДАНИЯ транзакции («%Y-%m-%d %H:%M:%S», локальное время).
    // Раньше журнал брал время из mtime файла: любой повторный save (ход
    // применения, откат) перезаписывал файл, и откаченная транзакция
    // «переезжала» вверх списка с чужим временем.
    std::string created_at;
    TransactionStatus status = TransactionStatus::Unprepared;
    std::vector<PreparedAction> prepared_actions;
    std::vector<ActionOutcome> outcomes;
    std::vector<ActionId> action_order;
    std::vector<ActionId> applied_action_order;
    RemediationError error = RemediationError::None;
    std::string detail;

    bool operator==(const TransactionRecord&) const = default;
};

// Локальная метка времени в единственном формате журнала. Формат фиксирован,
// потому что по нему же сортируется история (лексикографический порядок
// совпадает с хронологическим).
inline std::string formatLocalTimestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm parts{};
#if defined(_WIN32)
    localtime_s(&parts, &now);
#else
    localtime_r(&now, &parts);
#endif
    char buf[32] = {};
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &parts);
    return buf;
}

struct TransactionSummary {
    std::string transaction_id;
    TransactionStatus status = TransactionStatus::Unprepared;
    std::string created_at;
};

// Единственное правило «эту транзакцию ещё можно откатить». Движок
// (FixTransaction::rollback) применяет ровно его, а UI и BoostGate обязаны
// спрашивать здесь, а не выводить правило самостоятельно: пока каждый писал
// своё, кнопка «Откатить» в «Центре изменений» считала откатываемой только
// запись со статусом Applied и не видела частично применённые транзакции,
// которые движок откатывать умеет.
inline bool isRollbackEligible(const TransactionRecord& record) {
    switch (record.status) {
        case TransactionStatus::Applied:
        case TransactionStatus::Failed:
        case TransactionStatus::Cancelled:
        case TransactionStatus::RollbackFailed:
            break;
        default:
            // Reverted / RollingBack / Prepared / Unprepared / Applying —
            // либо уже откачено, либо откатывать нечего.
            return false;
    }
    return !record.applied_action_order.empty();
}

// Транзакция «применилась частично»: статус Applied (часть действий прошла),
// но какие-то действия честно упали. Нужно только для подписи в истории.
inline bool hasFailedOutcome(const TransactionRecord& record) {
    for (const auto& outcome : record.outcomes)
        if (outcome.attempted && outcome.status == ActionStatus::Failed) return true;
    return false;
}

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
            // 65535 — реальное значение MIB для туннельных адаптеров. Движок
            // пишет только kEngineMtuValue, но наблюдать может и такое: раньше
            // это рушило всю транзакцию целиком («observed state out of bounds»),
            // и пользователь не мог применить вообще ничего.
            return v.bytes >= 576 && v.bytes <= 65535;
        } else if constexpr (std::is_same_v<U, PowerPlanValue>) {
            return !v.identifier.empty() && v.identifier.size() <= 128;
        } else if constexpr (std::is_same_v<U, Cs2MaxPingValue>) {
            return v.max_ping >= 20 && v.max_ping <= 350;
        } else if constexpr (std::is_same_v<U, MouseAccelValue>) {
            return v.speed <= 1 && v.threshold1 <= 20 && v.threshold2 <= 20;
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
        case ActionId::GameMode: return "GameMode";
        case ActionId::MouseAccel: return "MouseAccel"; 
        case ActionId::Cs2FpsUnlock: return "Cs2FpsUnlock";
    }
    return "Unknown";
}

} // namespace remediation
} // namespace gno
