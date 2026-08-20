#include "remediation/json_backup_store.h"

#include "core/input_validation.h"
#include "core/json_persistence.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace gno {
namespace {

using json = nlohmann::json;

constexpr std::size_t kMaxTransactionBytes = 256 * 1024;
constexpr std::size_t kMaxResolvedTransactions = 100;
constexpr uint32_t kBackupVersion = 1;
constexpr char kProducer[] = "E2E4 Soft";

template <typename T>
Result<T> backupFailure(std::string detail) {
    Result<T> result;
    result.error = RemediationError::BackupFailed;
    result.detail = std::move(detail);
    return result;
}

Result<std::monostate> backupSuccess() {
    Result<std::monostate> result;
    result.error = RemediationError::None;
    return result;
}

bool isTransactionId(std::string_view id) noexcept {
    if (id.size() != 36) return false;
    constexpr std::array<std::size_t, 4> separators{8, 13, 18, 23};
    for (std::size_t index = 0; index < id.size(); ++index) {
        if (std::find(separators.begin(), separators.end(), index) != separators.end()) {
            if (id[index] != '-') return false;
            continue;
        }
        const bool digit = id[index] >= '0' && id[index] <= '9';
        const bool lower = id[index] >= 'a' && id[index] <= 'f';
        if (!digit && !lower) return false;
    }
    return id[14] == '4' && (id[19] == '8' || id[19] == '9' || id[19] == 'a' || id[19] == 'b');
}

std::filesystem::path transactionsRoot(const std::filesystem::path& storage_root) {
    const auto root = storage_root.empty() ? persistence::applicationDataRoot() : storage_root;
    return root / "GNO" / "remediation" / "transactions";
}

std::filesystem::path transactionPath(
    const std::filesystem::path& storage_root, std::string_view transaction_id) {
    return transactionsRoot(storage_root) / (std::string(transaction_id) + ".json");
}

bool isString(const json& value, std::size_t max_size, std::string& output) {
    if (!value.is_string()) return false;
    const auto& input = value.get_ref<const std::string&>();
    if (input.size() > max_size) return false;
    output = input;
    return true;
}

bool isUnsigned(const json& value, uint64_t maximum, uint64_t& output) {
    if (value.is_number_unsigned()) {
        const auto number = value.get<uint64_t>();
        if (number > maximum) return false;
        output = number;
        return true;
    }
    if (!value.is_number_integer()) return false;
    const auto number = value.get<int64_t>();
    if (number < 0 || static_cast<uint64_t>(number) > maximum) return false;
    output = static_cast<uint64_t>(number);
    return true;
}

bool isInt(const json& value, int64_t minimum, int64_t maximum, int64_t& output) {
    if (!value.is_number_integer() && !value.is_number_unsigned()) return false;
    if (value.is_number_unsigned()) {
        const auto number = value.get<uint64_t>();
        if (number > static_cast<uint64_t>(maximum)) return false;
        output = static_cast<int64_t>(number);
        return true;
    }
    const auto number = value.get<int64_t>();
    if (number < minimum || number > maximum) return false;
    output = number;
    return true;
}

template <typename Enum>
bool readEnum(const json& value, Enum& output, std::size_t count) {
    uint64_t number = 0;
    if (!isUnsigned(value, count - 1, number)) return false;
    output = static_cast<Enum>(number);
    return true;
}

json writeTarget(const ActionTarget& target) {
    return std::visit(
        [](const auto& value) -> json {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (std::is_same<Value, std::monostate>::value) {
                return {{"kind", "none"}};
            } else if constexpr (std::is_same<Value, InterfaceId>::value) {
                return {{"kind", "interface"}, {"value", value.value}};
            } else if constexpr (std::is_same<Value, ExecutableIdentity>::value) {
                return {{"kind", "executable"}, {"path", value.canonical_path}};
            } else {
                return {{"kind", "process"},
                        {"pid", value.pid},
                        {"creation_time", value.creation_time},
                        {"path", value.executable_path}};
            }
        },
        target);
}

std::optional<ActionTarget> readTarget(const json& value) {
    try {
        if (!value.is_object()) return std::nullopt;
        std::string kind;
        if (!isString(value.at("kind"), 16, kind)) return std::nullopt;
        if (kind == "none") return ActionTarget{std::monostate{}};
        if (kind == "interface") {
            std::string interface_id;
            if (!isString(value.at("value"), kMaxInterfaceIdLength, interface_id) ||
                interface_id.empty()) return std::nullopt;
            return ActionTarget{InterfaceId{std::move(interface_id)}};
        }
        if (kind == "executable") {
            std::string path;
            if (!isString(value.at("path"), kMaxExecutablePathLength, path) || path.empty()) {
                return std::nullopt;
            }
            return ActionTarget{ExecutableIdentity{std::move(path)}};
        }
        if (kind == "process") {
            uint64_t pid = 0;
            uint64_t creation_time = 0;
            std::string path;
            if (!isUnsigned(value.at("pid"), std::numeric_limits<uint32_t>::max(), pid) ||
                pid == 0 ||
                !isUnsigned(value.at("creation_time"), std::numeric_limits<uint64_t>::max(),
                            creation_time) ||
                creation_time == 0 ||
                !isString(value.at("path"), kMaxExecutablePathLength, path) || path.empty()) {
                return std::nullopt;
            }
            return ActionTarget{ProcessIdentity{static_cast<uint32_t>(pid), creation_time,
                                                std::move(path)}};
        }
    } catch (const json::exception&) {
    }
    return std::nullopt;
}

json writeValue(const ActionValue& action_value) {
    return std::visit(
        [](const auto& value) -> json {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (std::is_same<Value, std::monostate>::value) {
                return {{"kind", "none"}};
            } else if constexpr (std::is_same<Value, DnsValue>::value) {
                json servers = json::array();
                for (const auto& server : value.servers) servers.push_back(server.toString());
                return {{"kind", "dns"}, {"automatic", value.automatic}, {"servers", servers}};
            } else if constexpr (std::is_same<Value, MtuValue>::value) {
                return {{"kind", "mtu"}, {"bytes", value.bytes}};
            } else if constexpr (std::is_same<Value, TcpValue>::value) {
                json settings = json::array();
                for (const auto& setting : value.settings) {
                    settings.push_back({{"parameter", static_cast<unsigned>(setting.parameter)},
                                        {"existed", setting.existed}, {"value", setting.value}});
                }
                return {{"kind", "tcp"}, {"settings", settings}};
            } else if constexpr (std::is_same<Value, PowerPlanValue>::value) {
                return {{"kind", "power_plan"}, {"identifier", value.identifier}};
            } else if constexpr (std::is_same<Value, EnergyValue>::value) {
                return {{"kind", "energy"}, {"mode", static_cast<unsigned>(value.mode)}};
            } else if constexpr (std::is_same<Value, RegistryValue>::value) {
                json result = {{"kind", "registry"}, {"existed", value.existed}};
                std::visit(
                    [&result](const auto& scalar) {
                        using Scalar = std::decay_t<decltype(scalar)>;
                        if constexpr (std::is_same<Scalar, std::monostate>::value) {
                            result["value_kind"] = "none";
                        } else if constexpr (std::is_same<Scalar, uint32_t>::value) {
                            result["value_kind"] = "uint32";
                            result["value"] = scalar;
                        } else if constexpr (std::is_same<Scalar, int64_t>::value) {
                            result["value_kind"] = "int64";
                            result["value"] = scalar;
                        } else {
                            result["value_kind"] = "string";
                            result["value"] = scalar;
                        }
                    },
                    value.value);
                return result;
            } else if constexpr (std::is_same<Value, FullscreenValue>::value) {
                return {{"kind", "fullscreen"}, {"existed", value.existed},
                        {"flags", value.compatibility_flags}};
            } else if constexpr (std::is_same<Value, PriorityValue>::value) {
                return {{"kind", "priority"}, {"value", static_cast<unsigned>(value)}};
            } else {
                return {{"kind", "nice"}, {"value", value.value}};
            }
        },
        action_value);
}

std::optional<ActionValue> readValue(const json& value) {
    try {
        if (!value.is_object()) return std::nullopt;
        std::string kind;
        if (!isString(value.at("kind"), 32, kind)) return std::nullopt;
        if (kind == "none") return ActionValue{std::monostate{}};
        if (kind == "dns") {
            if (!value.at("automatic").is_boolean() || !value.at("servers").is_array() ||
                value.at("servers").size() > kMaxDnsServers) return std::nullopt;
            DnsValue dns;
            dns.automatic = value.at("automatic").get<bool>();
            for (const auto& server : value.at("servers")) {
                std::string text;
                if (!isString(server, 15, text)) return std::nullopt;
                const auto parsed = Ipv4Address::parse(text);
                if (!parsed) return std::nullopt;
                dns.servers.push_back(*parsed);
            }
            return ActionValue{std::move(dns)};
        }
        if (kind == "mtu") {
            uint64_t bytes = 0;
            if (!isUnsigned(value.at("bytes"), std::numeric_limits<uint32_t>::max(), bytes)) {
                return std::nullopt;
            }
            return ActionValue{MtuValue{static_cast<uint32_t>(bytes)}};
        }
        if (kind == "tcp") {
            const auto& settings = value.at("settings");
            if (!settings.is_array() || settings.size() > kMaxTcpSettings) return std::nullopt;
            TcpValue tcp;
            for (const auto& item : settings) {
                if (!item.is_object() || !item.at("existed").is_boolean()) return std::nullopt;
                TcpParameter parameter;
                int64_t setting_value = 0;
                if (!readEnum(item.at("parameter"), parameter, 3) ||
                    !isInt(item.at("value"), std::numeric_limits<int32_t>::min(),
                           std::numeric_limits<int32_t>::max(), setting_value)) return std::nullopt;
                tcp.settings.push_back(TcpSetting{parameter, item.at("existed").get<bool>(),
                                                  static_cast<int32_t>(setting_value)});
            }
            return ActionValue{std::move(tcp)};
        }
        if (kind == "power_plan") {
            std::string identifier;
            if (!isString(value.at("identifier"), kMaxPowerPlanIdLength, identifier) ||
                identifier.empty()) return std::nullopt;
            return ActionValue{PowerPlanValue{std::move(identifier)}};
        }
        if (kind == "energy") {
            EnergyMode mode;
            if (!readEnum(value.at("mode"), mode, 4)) return std::nullopt;
            return ActionValue{EnergyValue{mode}};
        }
        if (kind == "registry") {
            if (!value.at("existed").is_boolean()) return std::nullopt;
            std::string value_kind;
            if (!isString(value.at("value_kind"), 16, value_kind)) return std::nullopt;
            RegistryScalar scalar;
            if (value_kind == "none") {
                scalar = std::monostate{};
            } else if (value_kind == "uint32") {
                uint64_t number = 0;
                if (!isUnsigned(value.at("value"), std::numeric_limits<uint32_t>::max(), number)) {
                    return std::nullopt;
                }
                scalar = static_cast<uint32_t>(number);
            } else if (value_kind == "int64") {
                int64_t number = 0;
                if (!isInt(value.at("value"), std::numeric_limits<int64_t>::min(),
                           std::numeric_limits<int64_t>::max(), number)) return std::nullopt;
                scalar = number;
            } else if (value_kind == "string") {
                std::string text;
                if (!isString(value.at("value"), kMaxRegistryStringLength, text)) return std::nullopt;
                scalar = std::move(text);
            } else {
                return std::nullopt;
            }
            return ActionValue{RegistryValue{value.at("existed").get<bool>(), std::move(scalar)}};
        }
        if (kind == "fullscreen") {
            if (!value.at("existed").is_boolean()) return std::nullopt;
            std::string flags;
            if (!isString(value.at("flags"), kMaxRegistryStringLength, flags)) return std::nullopt;
            return ActionValue{FullscreenValue{value.at("existed").get<bool>(), std::move(flags)}};
        }
        if (kind == "priority") {
            PriorityValue priority;
            if (!readEnum(value.at("value"), priority, 6)) return std::nullopt;
            return ActionValue{priority};
        }
        if (kind == "nice") {
            int64_t nice = 0;
            if (!isInt(value.at("value"), kMinNiceValue, kMaxNiceValue, nice)) return std::nullopt;
            return ActionValue{NiceValue{static_cast<int>(nice)}};
        }
    } catch (const json::exception&) {
    }
    return std::nullopt;
}

json writeState(const ActionState& state) {
    return {{"id", static_cast<unsigned>(state.id)},
            {"status", static_cast<unsigned>(state.status)},
            {"value", writeValue(state.value)},
            {"detail", state.detail}};
}

std::optional<ActionState> readState(const json& value) {
    try {
        if (!value.is_object()) return std::nullopt;
        ActionId id;
        ActionStatus status;
        std::string detail;
        const auto action_value = readValue(value.at("value"));
        if (!readEnum(value.at("id"), id, 8) || !readEnum(value.at("status"), status, 7) ||
            !action_value || !isString(value.at("detail"), kMaxDetailLength, detail)) {
            return std::nullopt;
        }
        return ActionState{id, status, *action_value, std::move(detail)};
    } catch (const json::exception&) {
        return std::nullopt;
    }
}

json writePreparedAction(const PreparedAction& action) {
    return {{"id", static_cast<unsigned>(action.id)},
            {"target", writeTarget(action.target)},
            {"before", writeState(action.before)},
            {"proposed", writeState(action.proposed)},
            {"rollback_supported", action.rollback_supported}};
}

std::optional<PreparedAction> readPreparedAction(const json& value) {
    try {
        if (!value.is_object() || !value.at("rollback_supported").is_boolean()) return std::nullopt;
        ActionId id;
        const auto target = readTarget(value.at("target"));
        const auto before = readState(value.at("before"));
        const auto proposed = readState(value.at("proposed"));
        if (!readEnum(value.at("id"), id, 8) || !target || !before || !proposed ||
            before->id != id || proposed->id != id || !isBounded(*target) ||
            !isBounded(before->value) || !isBounded(proposed->value) ||
            before->detail.size() > kMaxDetailLength || proposed->detail.size() > kMaxDetailLength) {
            return std::nullopt;
        }
        return PreparedAction{id, *target, *before, *proposed,
                              value.at("rollback_supported").get<bool>()};
    } catch (const json::exception&) {
        return std::nullopt;
    }
}

json writeOutcome(const ActionOutcome& outcome) {
    return {{"id", static_cast<unsigned>(outcome.id)},
            {"status", static_cast<unsigned>(outcome.status)},
            {"error", static_cast<unsigned>(outcome.error)},
            {"attempted", outcome.attempted},
            {"state", writeState(outcome.state)},
            {"detail", outcome.detail},
            {"rollback_error", static_cast<unsigned>(outcome.rollback_error)},
            {"rollback_attempted", outcome.rollback_attempted},
            {"rollback_detail", outcome.rollback_detail}};
}

std::optional<ActionOutcome> readOutcome(const json& value) {
    try {
        if (!value.is_object() || !value.at("attempted").is_boolean() ||
            !value.at("rollback_attempted").is_boolean()) return std::nullopt;
        ActionId id;
        ActionStatus status;
        RemediationError error;
        RemediationError rollback_error;
        std::string detail;
        std::string rollback_detail;
        const auto state = readState(value.at("state"));
        if (!readEnum(value.at("id"), id, 8) || !readEnum(value.at("status"), status, 7) ||
            !readEnum(value.at("error"), error, 14) ||
            !readEnum(value.at("rollback_error"), rollback_error, 14) || !state ||
            state->id != id || !isBounded(state->value) ||
            !isString(value.at("detail"), kMaxDetailLength, detail) ||
            !isString(value.at("rollback_detail"), kMaxDetailLength, rollback_detail)) {
            return std::nullopt;
        }
        return ActionOutcome{id, status, error, value.at("attempted").get<bool>(), *state,
                             std::move(detail), rollback_error,
                             value.at("rollback_attempted").get<bool>(),
                             std::move(rollback_detail)};
    } catch (const json::exception&) {
        return std::nullopt;
    }
}

json writeRecord(const TransactionRecord& record) {
    json prepared = json::array();
    for (const auto& action : record.prepared_actions) prepared.push_back(writePreparedAction(action));
    json outcomes = json::array();
    for (const auto& outcome : record.outcomes) outcomes.push_back(writeOutcome(outcome));
    json action_order = json::array();
    for (const auto id : record.action_order) action_order.push_back(static_cast<unsigned>(id));
    json applied_order = json::array();
    for (const auto id : record.applied_action_order) applied_order.push_back(static_cast<unsigned>(id));
    return {{"schema_version", record.schema_version},
            {"transaction_id", record.transaction_id},
            {"status", static_cast<unsigned>(record.status)},
            {"prepared_actions", prepared},
            {"outcomes", outcomes},
            {"action_order", action_order},
            {"applied_action_order", applied_order},
            {"error", static_cast<unsigned>(record.error)},
            {"detail", record.detail}};
}

std::optional<std::vector<ActionId>> readActionOrder(const json& value, std::size_t maximum) {
    try {
        if (!value.is_array() || value.size() > maximum) return std::nullopt;
        std::vector<ActionId> result;
        result.reserve(value.size());
        for (const auto& item : value) {
            ActionId id;
            if (!readEnum(item, id, 8)) return std::nullopt;
            result.push_back(id);
        }
        return result;
    } catch (const json::exception&) {
        return std::nullopt;
    }
}

bool validRecordShape(const TransactionRecord& record) {
    if (record.schema_version != kBackupVersion || !isTransactionId(record.transaction_id) ||
        static_cast<std::size_t>(record.status) >= 9 ||
        static_cast<std::size_t>(record.error) >= 14 ||
        record.prepared_actions.size() > kMaxTransactionActions ||
        record.outcomes.size() != record.prepared_actions.size() ||
        record.action_order.size() > 2 * kMaxTransactionActions ||
        record.applied_action_order.size() > record.prepared_actions.size() ||
        record.detail.size() > kMaxDetailLength) return false;

    std::array<bool, kMaxTransactionActions> seen{};
    for (std::size_t index = 0; index < record.prepared_actions.size(); ++index) {
        const auto& action = record.prepared_actions[index];
        const auto id = static_cast<std::size_t>(action.id);
        if (id >= seen.size() || seen[id] || action.before.id != action.id ||
            action.proposed.id != action.id || !isBounded(action.target) ||
            static_cast<std::size_t>(action.before.status) >= 7 ||
            static_cast<std::size_t>(action.proposed.status) >= 7 ||
            !isBounded(action.before.value) || !isSafeProposed(action.proposed.value) ||
            action.before.detail.size() > kMaxDetailLength ||
            action.proposed.detail.size() > kMaxDetailLength) return false;
        seen[id] = true;
        const auto& outcome = record.outcomes[index];
        if (outcome.id != action.id || outcome.state.id != action.id ||
            static_cast<std::size_t>(outcome.status) >= 7 ||
            static_cast<std::size_t>(outcome.error) >= 14 ||
            static_cast<std::size_t>(outcome.rollback_error) >= 14 ||
            static_cast<std::size_t>(outcome.state.status) >= 7 ||
            !isBounded(outcome.state.value) || outcome.state.detail.size() > kMaxDetailLength ||
            outcome.detail.size() > kMaxDetailLength ||
            outcome.rollback_detail.size() > kMaxDetailLength) return false;
    }
    for (const auto id : record.action_order) {
        if (static_cast<std::size_t>(id) >= seen.size() ||
            !seen[static_cast<std::size_t>(id)]) return false;
    }
    for (std::size_t index = 0; index < record.applied_action_order.size(); ++index) {
        if (record.applied_action_order[index] != record.prepared_actions[index].id) return false;
    }
    return true;
}

std::optional<TransactionRecord> readRecord(const json& value, std::string_view expected_id) {
    try {
        if (!value.is_object()) return std::nullopt;
        uint64_t schema_version = 0;
        TransactionStatus status;
        RemediationError error;
        std::string transaction_id;
        std::string detail;
        if (!isUnsigned(value.at("schema_version"), kBackupVersion, schema_version) ||
            schema_version != kBackupVersion ||
            !isString(value.at("transaction_id"), kMaxTransactionIdLength, transaction_id) ||
            transaction_id != expected_id || !isTransactionId(transaction_id) ||
            !readEnum(value.at("status"), status, 9) ||
            !readEnum(value.at("error"), error, 14) ||
            !isString(value.at("detail"), kMaxDetailLength, detail)) return std::nullopt;

        const auto& prepared_json = value.at("prepared_actions");
        const auto& outcomes_json = value.at("outcomes");
        if (!prepared_json.is_array() || prepared_json.size() > kMaxTransactionActions ||
            !outcomes_json.is_array() || outcomes_json.size() != prepared_json.size()) {
            return std::nullopt;
        }
        TransactionRecord record;
        record.schema_version = static_cast<uint32_t>(schema_version);
        record.transaction_id = std::move(transaction_id);
        record.status = status;
        record.error = error;
        record.detail = std::move(detail);
        for (const auto& item : prepared_json) {
            const auto action = readPreparedAction(item);
            if (!action) return std::nullopt;
            record.prepared_actions.push_back(*action);
        }
        for (const auto& item : outcomes_json) {
            const auto outcome = readOutcome(item);
            if (!outcome) return std::nullopt;
            record.outcomes.push_back(*outcome);
        }
        const auto action_order = readActionOrder(value.at("action_order"), 2 * kMaxTransactionActions);
        const auto applied_order = readActionOrder(value.at("applied_action_order"),
                                                   kMaxTransactionActions);
        if (!action_order || !applied_order) return std::nullopt;
        record.action_order = *action_order;
        record.applied_action_order = *applied_order;
        return validRecordShape(record) ? std::optional<TransactionRecord>{std::move(record)}
                                        : std::nullopt;
    } catch (const json::exception&) {
        return std::nullopt;
    }
}

bool isResolved(const TransactionRecord& record) noexcept {
    return record.status == TransactionStatus::Reverted;
}

} // namespace

JsonBackupStore::JsonBackupStore(std::filesystem::path storage_root)
    : storage_root_(std::move(storage_root)) {}

Result<std::monostate> JsonBackupStore::save(const TransactionRecord& record) {
    if (!validRecordShape(record)) return backupFailure<std::monostate>("invalid transaction record");

    const auto path = transactionPath(storage_root_, record.transaction_id);
    std::error_code exists_error;
    if (std::filesystem::exists(path, exists_error)) {
        const auto existing = load(record.transaction_id);
        if (!existing.ok()) {
            return backupFailure<std::monostate>(
                "refusing to overwrite an unrecognized transaction backup");
        }
    } else if (exists_error) {
        return backupFailure<std::monostate>("could not inspect existing transaction backup");
    }

    const json document = {{"version", kBackupVersion}, {"producer", kProducer},
                           {"transaction", writeRecord(record)}};
    const auto content = document.dump();
    if (content.size() > kMaxTransactionBytes) {
        return backupFailure<std::monostate>("transaction backup exceeds 256 KiB");
    }
    if (!persistence::atomicWriteText(path, content)) {
        return backupFailure<std::monostate>("could not write transaction backup");
    }

    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> resolved;
    std::error_code error;
    const auto root = transactionsRoot(storage_root_);
    for (std::filesystem::directory_iterator iterator(root, error), end; !error && iterator != end;
         iterator.increment(error)) {
        const auto& entry = *iterator;
        if (!entry.is_regular_file(error) || error || entry.path().extension() != ".json") continue;
        const auto id = entry.path().stem().string();
        if (!isTransactionId(id)) continue;
        const auto loaded = load(id);
        if (!loaded.ok() || !isResolved(loaded.value)) continue;
        const auto modified = entry.last_write_time(error);
        if (error) continue;
        resolved.emplace_back(modified, entry.path());
    }
    std::sort(resolved.begin(), resolved.end(), [](const auto& left, const auto& right) {
        return left.first == right.first ? left.second < right.second : left.first < right.first;
    });
    while (resolved.size() > kMaxResolvedTransactions) {
        std::filesystem::remove(resolved.front().second, error);
        error.clear();
        resolved.erase(resolved.begin());
    }
    return backupSuccess();
}

Result<TransactionRecord> JsonBackupStore::load(std::string_view transaction_id) const {
    if (!isTransactionId(transaction_id)) return backupFailure<TransactionRecord>("invalid transaction id");
    const auto content = readBoundedFile(transactionPath(storage_root_, transaction_id).string(),
                                         kMaxTransactionBytes);
    if (!content) return backupFailure<TransactionRecord>("transaction backup is unavailable or too large");
    try {
        const auto document = json::parse(*content, nullptr, false);
        if (document.is_discarded() || !document.is_object() ||
            !document.contains("version") || !document.contains("producer") ||
            !document.contains("transaction")) {
            return backupFailure<TransactionRecord>("transaction backup is malformed");
        }
        uint64_t version = 0;
        std::string producer;
        if (!isUnsigned(document.at("version"), kBackupVersion, version) || version != kBackupVersion ||
            !isString(document.at("producer"), sizeof(kProducer) - 1, producer) || producer != kProducer) {
            return backupFailure<TransactionRecord>("transaction backup is not owned by this application");
        }
        const auto record = readRecord(document.at("transaction"), transaction_id);
        if (!record) return backupFailure<TransactionRecord>("transaction backup failed validation");
        Result<TransactionRecord> result;
        result.value = *record;
        result.error = RemediationError::None;
        return result;
    } catch (const json::exception&) {
        return backupFailure<TransactionRecord>("transaction backup is malformed");
    }
}

Result<std::vector<TransactionSummary>> JsonBackupStore::list() const {
    std::vector<TransactionSummary> summaries;
    std::error_code error;
    const auto root = transactionsRoot(storage_root_);
    if (!std::filesystem::exists(root, error)) {
        Result<std::vector<TransactionSummary>> result;
        result.value = std::move(summaries);
        result.error = error ? RemediationError::BackupFailed : RemediationError::None;
        result.detail = error ? "could not enumerate transaction backups" : "";
        return result;
    }
    for (std::filesystem::directory_iterator iterator(root, error), end; !error && iterator != end;
         iterator.increment(error)) {
        const auto& entry = *iterator;
        if (!entry.is_regular_file(error) || error || entry.path().extension() != ".json") continue;
        const auto id = entry.path().stem().string();
        if (!isTransactionId(id)) continue;
        const auto record = load(id);
        if (record.ok()) summaries.push_back({record.value.transaction_id, record.value.status});
    }
    if (error) return backupFailure<std::vector<TransactionSummary>>("could not enumerate transaction backups");
    std::sort(summaries.begin(), summaries.end(), [](const auto& left, const auto& right) {
        return left.transaction_id < right.transaction_id;
    });
    Result<std::vector<TransactionSummary>> result;
    result.value = std::move(summaries);
    result.error = RemediationError::None;
    return result;
}

} // namespace gno
