#include "doctest.h"
#include "remediation/backup_store.h"
#include "remediation/fix_action.h"
#include "remediation/fix_transaction.h"
#include "remediation/json_backup_store.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace {

using namespace gno;

const char* actionName(ActionId id) {
    switch (id) {
    case ActionId::PowerPlan:
        return "power";
    case ActionId::Dns:
        return "dns";
    case ActionId::Mtu:
        return "mtu";
    default:
        return "other";
    }
}

ActionValue originalValue(ActionId id) {
    switch (id) {
    case ActionId::PowerPlan:
        return PowerPlanValue{"balanced"};
    case ActionId::Dns:
        return DnsValue{true, {}};
    case ActionId::Mtu:
        return MtuValue{1500};
    default:
        return std::monostate{};
    }
}

ActionValue proposedValue(ActionId id) {
    switch (id) {
    case ActionId::PowerPlan:
        return PowerPlanValue{"performance"};
    case ActionId::Dns:
        return DnsValue{false, {}};
    case ActionId::Mtu:
        return MtuValue{1400};
    default:
        return std::monostate{};
    }
}

class TemporaryBackupRoot {
public:
    explicit TemporaryBackupRoot(const std::string& name)
        : path_(std::filesystem::temp_directory_path() / name) {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
        std::filesystem::create_directories(path_, error);
        REQUIRE_FALSE(error);
    }

    ~TemporaryBackupRoot() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

std::string transactionId(unsigned int value) {
    char suffix[13]{};
    std::snprintf(suffix, sizeof(suffix), "%012x", value);
    return std::string("00000000-0000-4000-8000-") + suffix;
}

Ipv4Address parsedAddress(const char* value) {
    const auto parsed = Ipv4Address::parse(value);
    REQUIRE(parsed);
    return *parsed;
}

ActionTarget persistedTarget(std::size_t index) {
    switch (static_cast<ActionId>(index)) {
    case ActionId::Dns:
    case ActionId::Mtu:
        return InterfaceId{"{01234567-89ab-cdef-0123-456789abcdef}"};
    case ActionId::FullscreenOptimizations:
        return ExecutableIdentity{"C:/Games/example.exe"};
    case ActionId::ProcessPriority:
        return ProcessIdentity{1234, 5678, "C:/Games/example.exe"};
    default:
        return std::monostate{};
    }
}

ActionValue persistedValueFor(ActionId id) {
    switch (id) {
    case ActionId::PowerPlan: return PowerPlanValue{"381b4222-f694-41f0-9685-ff5bb260df2e"};
    case ActionId::EnergyMode: return EnergyValue{EnergyMode::HighPower};
    case ActionId::GameDvr: return RegistryValue{true, uint32_t{7}};
    case ActionId::FullscreenOptimizations: return FullscreenValue{true, "~ DISABLEDXMAXIMIZEDWINDOWEDMODE"};
    case ActionId::TcpParameters: return TcpValue{{TcpSetting{TcpParameter::AutoTuningLevel, true, 1}}};
    case ActionId::Dns: return DnsValue{false, {parsedAddress("1.1.1.1")}};
    case ActionId::Mtu: return MtuValue{1450};
    case ActionId::ProcessPriority: return PriorityValue::AboveNormal;
    }
    return std::monostate{};
}

TransactionRecord persistedRecord(std::string id, TransactionStatus status = TransactionStatus::Applied) {
    TransactionRecord record;
    record.transaction_id = std::move(id);
    record.status = status;
    for (std::size_t index = 0; index < kMaxTransactionActions; ++index) {
        const auto action_id = static_cast<ActionId>(index);
        PreparedAction action;
        action.id = action_id;
        action.target = persistedTarget(index);
        action.before = ActionState{action_id, ActionStatus::Recommended,
                                    persistedValueFor(action_id), "before detail"};
        action.proposed = ActionState{action_id, ActionStatus::Applied,
                                      persistedValueFor(action_id), "proposed detail"};
        action.rollback_supported = true;
        record.prepared_actions.push_back(std::move(action));

        ActionOutcome outcome;
        outcome.id = action_id;
        outcome.status = status == TransactionStatus::Reverted ? ActionStatus::Reverted
                                                                : ActionStatus::Applied;
        outcome.attempted = true;
        outcome.state = ActionState{action_id, outcome.status, persistedValueFor(action_id),
                                    "outcome detail"};
        outcome.rollback_attempted = status == TransactionStatus::Reverted;
        record.outcomes.push_back(std::move(outcome));
        record.applied_action_order.push_back(action_id);
    }
    if (status == TransactionStatus::Reverted) {
        for (std::size_t index = kMaxTransactionActions; index > 0; --index) {
            record.action_order.push_back(static_cast<ActionId>(index - 1));
        }
    } else {
        record.action_order = record.applied_action_order;
    }
    return record;
}

void resetOutcome(ActionOutcome& outcome) {
    outcome.status = ActionStatus::NotChecked;
    outcome.error = RemediationError::None;
    outcome.attempted = false;
    outcome.state = ActionState{outcome.id, ActionStatus::NotChecked, std::monostate{}, {}};
    outcome.detail.clear();
    outcome.rollback_error = RemediationError::None;
    outcome.rollback_attempted = false;
    outcome.rollback_detail.clear();
}

void revertOutcome(ActionOutcome& outcome) {
    outcome.status = ActionStatus::Reverted;
    outcome.error = RemediationError::None;
    outcome.attempted = true;
    outcome.state.status = ActionStatus::Recommended;
    outcome.detail.clear();
    outcome.rollback_error = RemediationError::None;
    outcome.rollback_attempted = true;
    outcome.rollback_detail.clear();
}

TransactionRecord recordWithAppliedPrefix(
    std::string id, std::size_t applied_count, TransactionStatus status) {
    auto record = persistedRecord(std::move(id));
    record.status = status;
    record.action_order.resize(applied_count);
    record.applied_action_order.resize(applied_count);
    for (std::size_t index = applied_count; index < record.outcomes.size(); ++index) {
        resetOutcome(record.outcomes[index]);
    }
    return record;
}

std::filesystem::path backupPath(const TemporaryBackupRoot& root, const std::string& id) {
    return root.path() / "GNO" / "remediation" / "transactions" / (id + ".json");
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void writeText(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    REQUIRE(output);
    output << content;
    REQUIRE(output);
}

template <typename T>
Result<T> success(T value) {
    Result<T> result;
    result.value = std::move(value);
    result.error = RemediationError::None;
    return result;
}

template <typename T>
Result<T> failure(RemediationError error, std::string detail = {}) {
    Result<T> result;
    result.error = error;
    result.detail = std::move(detail);
    return result;
}

class FakeAction final : public FixAction {
public:
    FakeAction(ActionId action_id, std::vector<std::string>& log,
               std::shared_ptr<ActionValue> state = {})
        : action_id_(action_id),
          log_(log),
          state_(state ? std::move(state)
                       : std::make_shared<ActionValue>(originalValue(action_id))) {}

    ActionId id() const noexcept override { return action_id_; }

    Result<ActionState> observe(
        const ActionTarget&, const CancellationToken&) const override {
        log_.push_back(std::string("observe:") + actionName(action_id_));
        if (observe_error != RemediationError::None) {
            return failure<ActionState>(observe_error, "observe failed");
        }
        return success(ActionState{action_id_, ActionStatus::Recommended,
                                   *state_, {}});
    }

    Result<PreparedAction> prepare(
        const ActionTarget& target, const ActionState& observed) const override {
        log_.push_back(std::string("prepare:") + actionName(action_id_));
        if (prepare_error != RemediationError::None) {
            return failure<PreparedAction>(prepare_error, "prepare failed");
        }
        PreparedAction prepared;
        prepared.id = action_id_;
        prepared.target = target;
        prepared.before = observed;
        if (fabricate_before) {
            prepared.before.value = proposedValue(action_id_);
        }
        prepared.proposed = ActionState{action_id_, ActionStatus::Recommended,
                                        proposed_override.value_or(
                                            proposedValue(action_id_)), {}};
        prepared.rollback_supported = rollback_supported;
        return success(std::move(prepared));
    }

    Result<ActionState> apply(
        const PreparedAction& prepared, const CancellationToken&) override {
        log_.push_back(std::string("apply:") + actionName(action_id_));
        if (on_apply) {
            on_apply();
        }
        if (apply_error != RemediationError::None) {
            return failure<ActionState>(apply_error, "apply failed");
        }
        if (!apply_observation_mismatch) {
            *state_ = prepared.proposed.value;
        }
        auto value = apply_return_override.value_or(prepared.proposed.value);
        return success(ActionState{action_id_, ActionStatus::Applied,
                                   std::move(value), {}});
    }

    Result<ActionState> rollback(
        const PreparedAction& prepared, const CancellationToken&) override {
        log_.push_back(std::string("rollback:") + actionName(action_id_));
        if (rollback_error != RemediationError::None) {
            return failure<ActionState>(rollback_error, "rollback failed");
        }
        if (rollback_observed_override) {
            *state_ = *rollback_observed_override;
        } else if (!rollback_observation_mismatch) {
            *state_ = prepared.before.value;
        }
        auto value = rollback_return_override.value_or(prepared.before.value);
        return success(ActionState{action_id_, ActionStatus::Reverted,
                                   std::move(value), {}});
    }

    RemediationError observe_error = RemediationError::None;
    RemediationError prepare_error = RemediationError::None;
    RemediationError apply_error = RemediationError::None;
    RemediationError rollback_error = RemediationError::None;
    bool fabricate_before = false;
    bool rollback_supported = true;
    bool apply_observation_mismatch = false;
    bool rollback_observation_mismatch = false;
    std::optional<ActionValue> proposed_override;
    std::optional<ActionValue> apply_return_override;
    std::optional<ActionValue> rollback_return_override;
    std::optional<ActionValue> rollback_observed_override;
    std::function<void()> on_apply;

private:
    ActionId action_id_;
    std::vector<std::string>& log_;
    std::shared_ptr<ActionValue> state_;
};

class FakeBackupStore final : public BackupStore {
public:
    explicit FakeBackupStore(std::vector<std::string>& log) : log_(log) {}

    Result<std::monostate> save(const TransactionRecord& record) override {
        ++save_calls;
        if (records.empty()) {
            log_.push_back("save");
        }
        records.push_back(record);
        if (fail_next_save || (fail_on_save != 0 && save_calls == fail_on_save)) {
            fail_next_save = false;
            return failure<std::monostate>(RemediationError::BackupFailed, "save failed");
        }
        return success(std::monostate{});
    }

    Result<TransactionRecord> load(std::string_view transaction_id) const override {
        for (auto iterator = records.rbegin(); iterator != records.rend(); ++iterator) {
            if (iterator->transaction_id == transaction_id) {
                return success(*iterator);
            }
        }
        return failure<TransactionRecord>(RemediationError::BackupFailed, "not found");
    }

    Result<std::vector<TransactionSummary>> list() const override {
        return success(std::vector<TransactionSummary>{});
    }

    std::vector<TransactionRecord> records;
    bool fail_next_save = false;
    std::size_t fail_on_save = 0;
    std::size_t save_calls = 0;

private:
    std::vector<std::string>& log_;
};

std::unique_ptr<FixTransaction> makeTransaction(
    FakeBackupStore& store,
    std::vector<std::unique_ptr<FixAction>> actions,
    std::string transaction_id = "00000000-0000-4000-8000-000000000001") {
    std::vector<ActionTarget> targets(actions.size(), std::monostate{});
    return std::make_unique<FixTransaction>(
        std::move(transaction_id), std::move(actions), std::move(targets), store);
}

std::vector<std::unique_ptr<FixAction>> twoActions(
    std::vector<std::string>& log,
    FakeAction** power = nullptr,
    FakeAction** dns = nullptr) {
    std::vector<std::unique_ptr<FixAction>> actions;
    auto power_action = std::make_unique<FakeAction>(ActionId::PowerPlan, log);
    auto dns_action = std::make_unique<FakeAction>(ActionId::Dns, log);
    if (power) {
        *power = power_action.get();
    }
    if (dns) {
        *dns = dns_action.get();
    }
    actions.push_back(std::move(power_action));
    actions.push_back(std::move(dns_action));
    return actions;
}

std::vector<std::unique_ptr<FixAction>> oneAction(
    std::vector<std::string>& log, ActionId id, FakeAction** action = nullptr,
    std::shared_ptr<ActionValue> state = {}) {
    std::vector<std::unique_ptr<FixAction>> actions;
    auto fake = std::make_unique<FakeAction>(id, log, std::move(state));
    if (action) {
        *action = fake.get();
    }
    actions.push_back(std::move(fake));
    return actions;
}

void requirePrepared(FixTransaction& transaction, const CancellationToken& token) {
    const auto prepared = transaction.prepare(token);
    REQUIRE(prepared.ok());
    CHECK(prepared.value.status == TransactionStatus::Prepared);
}

static_assert(std::has_virtual_destructor<FixAction>::value);
static_assert(std::has_virtual_destructor<BackupStore>::value);
static_assert(std::variant_size<ActionValue>::value == 10);

} // namespace

TEST_SUITE_BEGIN("remediation");

TEST_CASE("remediation domain uses closed typed values") {
    const gno::ActionValue dns = gno::DnsValue{true, {}};
    const gno::ActionValue unavailable = std::monostate{};
    CHECK(std::holds_alternative<gno::DnsValue>(dns));
    CHECK(std::holds_alternative<std::monostate>(unavailable));

    gno::Result<gno::ActionState> result;
    CHECK_FALSE(result.ok());
    CHECK(result.error == gno::RemediationError::InternalFailure);
}

TEST_CASE("transaction completes all preflight and persists Prepared before mutation") {
    std::vector<std::string> log;
    FakeBackupStore store(log);
    auto transaction = makeTransaction(store, twoActions(log));
    const gno::CancellationToken token;

    requirePrepared(*transaction, token);
    CHECK(log == std::vector<std::string>{"observe:power", "prepare:power",
                                          "observe:dns", "prepare:dns"});

    const auto executed = transaction->execute(token);

    REQUIRE(executed.ok());
    CHECK(log == std::vector<std::string>{"observe:power", "prepare:power",
                                          "observe:dns", "prepare:dns",
                                          "observe:power", "observe:dns", "save",
                                          "apply:power", "observe:power",
                                          "apply:dns", "observe:dns"});
    REQUIRE(store.records.size() == 3);
    CHECK(store.records[0].status == gno::TransactionStatus::Prepared);
    CHECK(store.records[1].outcomes[0].status == gno::ActionStatus::Applied);
    CHECK(store.records[1].outcomes[1].status == gno::ActionStatus::NotChecked);
    CHECK(store.records[2].status == gno::TransactionStatus::Applied);
}

TEST_CASE("backup failure prevents every mutation") {
    std::vector<std::string> log;
    FakeBackupStore store(log);
    store.fail_next_save = true;
    auto transaction = makeTransaction(store, twoActions(log));
    const gno::CancellationToken token;
    requirePrepared(*transaction, token);

    const auto result = transaction->execute(token);

    CHECK_FALSE(result.ok());
    CHECK(result.error == gno::RemediationError::BackupFailed);
    CHECK(log == std::vector<std::string>{"observe:power", "prepare:power",
                                          "observe:dns", "prepare:dns",
                                          "observe:power", "observe:dns", "save"});
}

TEST_CASE("apply stops at first failure and preserves not-attempted outcomes") {
    std::vector<std::string> log;
    FakeBackupStore store(log);
    FakeAction* power = nullptr;
    FakeAction* dns = nullptr;
    auto actions = twoActions(log, &power, &dns);
    power->apply_error = gno::RemediationError::ApplyFailed;
    auto transaction = makeTransaction(store, std::move(actions));
    const gno::CancellationToken token;
    requirePrepared(*transaction, token);

    const auto result = transaction->execute(token);

    CHECK_FALSE(result.ok());
    CHECK(result.error == gno::RemediationError::ApplyFailed);
    CHECK(log.back() == "apply:power");
    REQUIRE(result.value.outcomes.size() == 2);
    CHECK(result.value.outcomes[0].status == gno::ActionStatus::Failed);
    CHECK(result.value.outcomes[0].attempted);
    CHECK(result.value.outcomes[1].status == gno::ActionStatus::NotChecked);
    CHECK_FALSE(result.value.outcomes[1].attempted);
    (void)dns;
}

TEST_CASE("verification mismatch is a failure") {
    std::vector<std::string> log;
    FakeBackupStore store(log);
    FakeAction* power = nullptr;
    auto actions = twoActions(log, &power, nullptr);
    power->apply_observation_mismatch = true;
    auto transaction = makeTransaction(store, std::move(actions));
    const gno::CancellationToken token;
    requirePrepared(*transaction, token);

    const auto result = transaction->execute(token);

    CHECK_FALSE(result.ok());
    CHECK(result.error == gno::RemediationError::VerificationMismatch);
    CHECK(result.value.outcomes[0].error == gno::RemediationError::VerificationMismatch);
    CHECK(result.value.outcomes[0].state.value ==
          originalValue(gno::ActionId::PowerPlan));
    CHECK(store.records.back().outcomes[0].state.value ==
          originalValue(gno::ActionId::PowerPlan));
    CHECK(result.value.outcomes[1].status == gno::ActionStatus::NotChecked);
    CHECK(log[log.size() - 2] == "apply:power");
    CHECK(log.back() == "observe:power");
}

TEST_CASE("apply verification trusts fresh observation rather than mutation return") {
    std::vector<std::string> log;
    FakeBackupStore store(log);
    FakeAction* power = nullptr;
    auto actions = oneAction(log, gno::ActionId::PowerPlan, &power);
    power->apply_return_override = originalValue(gno::ActionId::PowerPlan);
    auto transaction = makeTransaction(store, std::move(actions));
    const gno::CancellationToken token;
    requirePrepared(*transaction, token);

    const auto result = transaction->execute(token);

    REQUIRE(result.ok());
    CHECK(result.value.outcomes[0].state.value ==
          proposedValue(gno::ActionId::PowerPlan));
    REQUIRE(store.records.size() == 2);
    CHECK(store.records.back().outcomes[0].state.value ==
          proposedValue(gno::ActionId::PowerPlan));
    CHECK(log[log.size() - 2] == "apply:power");
    CHECK(log.back() == "observe:power");
}

TEST_CASE("execute rejects a stale prepared before-state before backup or mutation") {
    std::vector<std::string> first_log;
    std::vector<std::string> second_log;
    FakeBackupStore first_store(first_log);
    FakeBackupStore second_store(second_log);
    auto shared_state = std::make_shared<gno::ActionValue>(
        originalValue(gno::ActionId::PowerPlan));
    auto first = makeTransaction(
        first_store,
        oneAction(first_log, gno::ActionId::PowerPlan, nullptr, shared_state));
    std::vector<std::unique_ptr<gno::FixAction>> second_actions;
    second_actions.push_back(std::make_unique<FakeAction>(
        gno::ActionId::PowerPlan, second_log, shared_state));
    second_actions.push_back(std::make_unique<FakeAction>(
        gno::ActionId::Dns, second_log));
    auto second = makeTransaction(
        second_store, std::move(second_actions),
        "00000000-0000-4000-8000-000000000002");
    const gno::CancellationToken token;
    requirePrepared(*first, token);
    requirePrepared(*second, token);
    REQUIRE(first->execute(token).ok());

    const auto stale = second->execute(token);

    CHECK_FALSE(stale.ok());
    CHECK(stale.error == gno::RemediationError::PreflightFailed);
    CHECK(second_store.records.empty());
    CHECK(second_log == std::vector<std::string>{
                            "observe:power", "prepare:power",
                            "observe:dns", "prepare:dns",
                            "observe:power", "observe:dns"});
}

TEST_CASE("prepare rejects a fabricated backup that differs from observation") {
    std::vector<std::string> log;
    FakeBackupStore store(log);
    FakeAction* power = nullptr;
    auto actions = oneAction(log, gno::ActionId::PowerPlan, &power);
    power->fabricate_before = true;
    auto transaction = makeTransaction(store, std::move(actions));

    const auto result = transaction->prepare({});

    CHECK_FALSE(result.ok());
    CHECK(result.error == gno::RemediationError::PreflightFailed);
    CHECK(store.records.empty());
}

TEST_CASE("transaction preparation is one-shot and cannot erase applied bookkeeping") {
    std::vector<std::string> log;
    FakeBackupStore store(log);
    auto transaction = makeTransaction(
        store, oneAction(log, gno::ActionId::PowerPlan));
    const gno::CancellationToken token;
    requirePrepared(*transaction, token);
    REQUIRE(transaction->execute(token).ok());
    const auto before_retry = transaction->record();
    const auto log_size = log.size();

    const auto retried = transaction->prepare(token);

    CHECK_FALSE(retried.ok());
    CHECK(retried.error == gno::RemediationError::PreflightFailed);
    CHECK(transaction->record().status == gno::TransactionStatus::Applied);
    CHECK(transaction->record().outcomes[0].status == gno::ActionStatus::Applied);
    CHECK(transaction->record().applied_action_order == before_retry.applied_action_order);
    CHECK(log.size() == log_size);
}

TEST_CASE("cancellation stops before the next action") {
    std::vector<std::string> log;
    FakeBackupStore store(log);
    FakeAction* power = nullptr;
    auto actions = twoActions(log, &power, nullptr);
    gno::CancellationSource source;
    power->on_apply = [&source] { source.cancel(); };
    auto transaction = makeTransaction(store, std::move(actions));
    const auto token = source.token();
    requirePrepared(*transaction, token);

    const auto result = transaction->execute(token);

    CHECK_FALSE(result.ok());
    CHECK(result.error == gno::RemediationError::Cancelled);
    CHECK(result.value.outcomes[0].status == gno::ActionStatus::Applied);
    CHECK(result.value.outcomes[1].status == gno::ActionStatus::NotChecked);
    CHECK(log[log.size() - 2] == "apply:power");
    CHECK(log.back() == "observe:power");
}

TEST_CASE("rollback visits only verified applied actions in reverse order") {
    std::vector<std::string> log;
    FakeBackupStore store(log);
    FakeAction* power = nullptr;
    FakeAction* dns = nullptr;
    auto actions = twoActions(log, &power, &dns);
    power->rollback_return_override = proposedValue(gno::ActionId::PowerPlan);
    dns->rollback_return_override = proposedValue(gno::ActionId::Dns);
    auto transaction = makeTransaction(store, std::move(actions));
    const gno::CancellationToken token;
    requirePrepared(*transaction, token);
    REQUIRE(transaction->execute(token).ok());

    const auto result = transaction->rollback(token);

    REQUIRE(result.ok());
    CHECK(result.value.action_order ==
          std::vector<gno::ActionId>{gno::ActionId::Dns, gno::ActionId::PowerPlan});
    REQUIRE(result.value.outcomes.size() == 2);
    CHECK(result.value.outcomes[0].status == gno::ActionStatus::Reverted);
    CHECK(result.value.outcomes[1].status == gno::ActionStatus::Reverted);
    CHECK(log[log.size() - 4] == "rollback:dns");
    CHECK(log[log.size() - 3] == "observe:dns");
    CHECK(log[log.size() - 2] == "rollback:power");
    CHECK(log.back() == "observe:power");
}

TEST_CASE("rollback observation mismatch remains applied and succeeds on retry") {
    std::vector<std::string> log;
    FakeBackupStore store(log);
    FakeAction* power = nullptr;
    FakeAction* dns = nullptr;
    auto actions = twoActions(log, &power, &dns);
    auto transaction = makeTransaction(store, std::move(actions));
    const gno::CancellationToken token;
    requirePrepared(*transaction, token);
    REQUIRE(transaction->execute(token).ok());
    power->rollback_observed_override = gno::PowerPlanValue{"eco"};

    const auto failed = transaction->rollback(token);

    CHECK_FALSE(failed.ok());
    CHECK(failed.error == gno::RemediationError::RollbackFailed);
    CHECK(failed.value.status == gno::TransactionStatus::RollbackFailed);
    CHECK(failed.value.outcomes[0].status == gno::ActionStatus::Applied);
    CHECK(failed.value.outcomes[0].rollback_error ==
          gno::RemediationError::VerificationMismatch);
    CHECK(failed.value.outcomes[0].state.value ==
          gno::ActionValue{gno::PowerPlanValue{"eco"}});
    CHECK(store.records.back().outcomes[0].state.value ==
          gno::ActionValue{gno::PowerPlanValue{"eco"}});
    CHECK(failed.value.outcomes[1].status == gno::ActionStatus::Reverted);

    power->rollback_observed_override.reset();
    const auto retried = transaction->rollback(token);

    REQUIRE(retried.ok());
    CHECK(retried.value.status == gno::TransactionStatus::Reverted);
    CHECK(retried.value.action_order ==
          std::vector<gno::ActionId>{gno::ActionId::PowerPlan});
    CHECK(retried.value.outcomes[0].status == gno::ActionStatus::Reverted);
    CHECK(retried.value.outcomes[0].rollback_error == gno::RemediationError::None);
}

TEST_CASE("rollback recovers a verified applied prefix after transition save failure") {
    std::vector<std::string> log;
    FakeBackupStore store(log);
    store.fail_on_save = 2;
    auto transaction = makeTransaction(
        store, oneAction(log, gno::ActionId::PowerPlan));
    const gno::CancellationToken token;
    requirePrepared(*transaction, token);

    const auto execution = transaction->execute(token);

    CHECK(execution.error == gno::RemediationError::BackupFailed);
    CHECK(execution.value.status == gno::TransactionStatus::Failed);
    CHECK(execution.value.outcomes[0].status == gno::ActionStatus::Applied);
    CHECK(execution.value.applied_action_order ==
          std::vector<gno::ActionId>{gno::ActionId::PowerPlan});

    const auto recovered = transaction->rollback(token);

    REQUIRE(recovered.ok());
    CHECK(recovered.value.status == gno::TransactionStatus::Reverted);
    CHECK(recovered.value.outcomes[0].status == gno::ActionStatus::Reverted);
    CHECK(recovered.value.action_order ==
          std::vector<gno::ActionId>{gno::ActionId::PowerPlan});
}

TEST_CASE("rollback recovery preserves unresolved prefix after reverted-state save failure") {
    std::vector<std::string> log;
    FakeBackupStore store(log);
    store.fail_on_save = 5;
    auto transaction = makeTransaction(store, twoActions(log));
    const gno::CancellationToken token;
    requirePrepared(*transaction, token);
    REQUIRE(transaction->execute(token).ok());

    const auto interrupted = transaction->rollback(token);

    CHECK(interrupted.error == gno::RemediationError::BackupFailed);
    CHECK(interrupted.value.status == gno::TransactionStatus::RollbackFailed);
    CHECK(interrupted.value.outcomes[0].status == gno::ActionStatus::Applied);
    CHECK(interrupted.value.outcomes[1].status == gno::ActionStatus::Reverted);
    CHECK(interrupted.value.status != gno::TransactionStatus::Reverted);

    const auto recovered = transaction->rollback(token);

    REQUIRE(recovered.ok());
    CHECK(recovered.value.status == gno::TransactionStatus::Reverted);
    CHECK(recovered.value.outcomes[0].status == gno::ActionStatus::Reverted);
    CHECK(recovered.value.outcomes[1].status == gno::ActionStatus::Reverted);
    CHECK(recovered.value.action_order ==
          std::vector<gno::ActionId>{gno::ActionId::PowerPlan});
}

TEST_CASE("rollback freshness checks every unresolved action before any rollback mutation") {
    std::vector<std::string> log;
    FakeBackupStore store(log);
    auto power_state = std::make_shared<gno::ActionValue>(
        originalValue(gno::ActionId::PowerPlan));
    auto dns_state = std::make_shared<gno::ActionValue>(
        originalValue(gno::ActionId::Dns));
    std::vector<std::unique_ptr<gno::FixAction>> actions;
    actions.push_back(std::make_unique<FakeAction>(
        gno::ActionId::PowerPlan, log, power_state));
    actions.push_back(std::make_unique<FakeAction>(
        gno::ActionId::Dns, log, dns_state));
    auto transaction = makeTransaction(store, std::move(actions));
    const gno::CancellationToken token;
    requirePrepared(*transaction, token);
    REQUIRE(transaction->execute(token).ok());
    *power_state = originalValue(gno::ActionId::PowerPlan);
    const auto saves_before = store.save_calls;

    const auto result = transaction->rollback(token);

    CHECK_FALSE(result.ok());
    CHECK(result.error == gno::RemediationError::RollbackFailed);
    CHECK(store.save_calls == saves_before);
    CHECK(result.value.status == gno::TransactionStatus::Applied);
    CHECK(result.value.outcomes[0].status == gno::ActionStatus::Applied);
    CHECK(result.value.outcomes[1].status == gno::ActionStatus::Applied);
    CHECK(log[log.size() - 2] == "observe:power");
    CHECK(log.back() == "observe:dns");
}

TEST_CASE("rollback rejects transactions without an eligible reversible applied prefix") {
    SUBCASE("Prepared is ineligible") {
        std::vector<std::string> log;
        FakeBackupStore store(log);
        auto transaction = makeTransaction(
            store, oneAction(log, gno::ActionId::PowerPlan));
        requirePrepared(*transaction, {});
        const auto before = log.size();

        const auto result = transaction->rollback({});

        CHECK(result.error == gno::RemediationError::RollbackFailed);
        CHECK(log.size() == before);
    }

    SUBCASE("backup failure is ineligible") {
        std::vector<std::string> log;
        FakeBackupStore store(log);
        store.fail_next_save = true;
        auto transaction = makeTransaction(
            store, oneAction(log, gno::ActionId::PowerPlan));
        requirePrepared(*transaction, {});
        REQUIRE(transaction->execute({}).error == gno::RemediationError::BackupFailed);

        const auto result = transaction->rollback({});

        CHECK(result.error == gno::RemediationError::RollbackFailed);
        CHECK(log.back() == "save");
    }

    SUBCASE("rollback unsupported is ineligible") {
        std::vector<std::string> log;
        FakeBackupStore store(log);
        FakeAction* power = nullptr;
        auto actions = oneAction(log, gno::ActionId::PowerPlan, &power);
        power->rollback_supported = false;
        auto transaction = makeTransaction(store, std::move(actions));
        requirePrepared(*transaction, {});
        REQUIRE(transaction->execute({}).ok());
        const auto before = log.size();

        const auto result = transaction->rollback({});

        CHECK(result.error == gno::RemediationError::RollbackFailed);
        CHECK(log.size() == before);
    }

    SUBCASE("already reverted is ineligible") {
        std::vector<std::string> log;
        FakeBackupStore store(log);
        auto transaction = makeTransaction(
            store, oneAction(log, gno::ActionId::PowerPlan));
        requirePrepared(*transaction, {});
        REQUIRE(transaction->execute({}).ok());
        REQUIRE(transaction->rollback({}).ok());
        const auto before = log.size();

        const auto result = transaction->rollback({});

        CHECK(result.error == gno::RemediationError::RollbackFailed);
        CHECK(log.size() == before);
    }
}

TEST_CASE("proposed process priority values are restricted to safe ranges") {
    const std::vector<gno::ActionValue> unsafe = {
        gno::PriorityValue::Realtime, gno::NiceValue{-21}, gno::NiceValue{20}};
    for (const auto& proposed : unsafe) {
        std::vector<std::string> log;
        FakeBackupStore store(log);
        FakeAction* priority = nullptr;
        auto observed = std::make_shared<gno::ActionValue>(
            gno::PriorityValue::Realtime);
        auto actions = oneAction(
            log, gno::ActionId::ProcessPriority, &priority, observed);
        priority->proposed_override = proposed;
        auto transaction = makeTransaction(store, std::move(actions));

        const auto result = transaction->prepare({});

        CHECK_FALSE(result.ok());
        CHECK(result.error == gno::RemediationError::PreflightFailed);
    }

    for (const auto nice : {-20, 19}) {
        std::vector<std::string> log;
        FakeBackupStore store(log);
        FakeAction* priority = nullptr;
        auto actions = oneAction(log, gno::ActionId::ProcessPriority, &priority);
        priority->proposed_override = gno::NiceValue{nice};
        auto transaction = makeTransaction(store, std::move(actions));
        CHECK(transaction->prepare({}).ok());
    }
}

TEST_CASE("concurrent and reentrant execute calls return Busy") {
    std::vector<std::string> log;
    FakeBackupStore store(log);
    FakeAction* power = nullptr;
    auto actions = twoActions(log, &power, nullptr);
    auto transaction = makeTransaction(store, std::move(actions));
    const gno::CancellationToken token;
    requirePrepared(*transaction, token);

    std::mutex gate_mutex;
    std::condition_variable gate;
    bool entered = false;
    bool release = false;
    gno::RemediationError reentrant_error = gno::RemediationError::None;
    power->on_apply = [&] {
        reentrant_error = transaction->execute(token).error;
        std::unique_lock<std::mutex> lock(gate_mutex);
        entered = true;
        gate.notify_one();
        gate.wait(lock, [&] { return release; });
    };

    gno::Result<gno::TransactionRecord> first;
    std::thread worker([&] { first = transaction->execute(token); });
    {
        std::unique_lock<std::mutex> lock(gate_mutex);
        gate.wait(lock, [&] { return entered; });
    }

    const auto concurrent = transaction->execute(token);
    CHECK(concurrent.error == gno::RemediationError::Busy);
    CHECK(reentrant_error == gno::RemediationError::Busy);

    {
        std::lock_guard<std::mutex> lock(gate_mutex);
        release = true;
    }
    gate.notify_one();
    worker.join();
    CHECK(first.ok());
}

TEST_CASE("different transactions cannot execute concurrently") {
    std::vector<std::string> first_log;
    std::vector<std::string> second_log;
    FakeBackupStore first_store(first_log);
    FakeBackupStore second_store(second_log);
    FakeAction* first_power = nullptr;
    auto first_actions = twoActions(first_log, &first_power, nullptr);
    auto first = makeTransaction(first_store, std::move(first_actions));
    auto second = makeTransaction(
        second_store, twoActions(second_log),
        "00000000-0000-4000-8000-000000000002");
    const gno::CancellationToken token;
    requirePrepared(*first, token);
    requirePrepared(*second, token);

    std::mutex gate_mutex;
    std::condition_variable gate;
    bool entered = false;
    bool release = false;
    first_power->on_apply = [&] {
        std::unique_lock<std::mutex> lock(gate_mutex);
        entered = true;
        gate.notify_one();
        gate.wait(lock, [&] { return release; });
    };

    gno::Result<gno::TransactionRecord> first_result;
    std::thread worker([&] { first_result = first->execute(token); });
    {
        std::unique_lock<std::mutex> lock(gate_mutex);
        gate.wait(lock, [&] { return entered; });
    }

    const auto second_result = second->execute(token);
    CHECK(second_result.error == gno::RemediationError::Busy);

    {
        std::lock_guard<std::mutex> lock(gate_mutex);
        release = true;
    }
    gate.notify_one();
    worker.join();
    CHECK(first_result.ok());
}

TEST_CASE("preflight rejects oversized domain inputs without calling actions") {
    std::vector<std::string> log;
    FakeBackupStore store(log);
    auto actions = twoActions(log);
    std::vector<gno::ActionTarget> targets;
    targets.emplace_back(gno::InterfaceId{std::string(gno::kMaxInterfaceIdLength + 1, 'x')});
    targets.emplace_back(std::monostate{});
    gno::FixTransaction transaction(
        "00000000-0000-4000-8000-000000000001",
        std::move(actions), std::move(targets), store);

    const auto result = transaction.prepare({});

    CHECK_FALSE(result.ok());
    CHECK(result.error == gno::RemediationError::InvalidTarget);
    CHECK(result.detail.size() <= gno::kMaxDetailLength);
    CHECK(log.empty());
}

TEST_CASE("JSON backups round-trip all typed remediation values") {
    TemporaryBackupRoot root("gno-json-backup-round-trip");
    JsonBackupStore store(root.path());
    const auto record = persistedRecord(transactionId(1));

    REQUIRE(store.save(record).ok());
    const auto loaded = store.load(record.transaction_id);

    REQUIRE(loaded.ok());
    CHECK(loaded.value.transaction_id == record.transaction_id);
    CHECK(loaded.value.status == record.status);
    CHECK(loaded.value.prepared_actions.size() == record.prepared_actions.size());
    CHECK(loaded.value.outcomes.size() == record.outcomes.size());
    for (std::size_t index = 0; index < record.prepared_actions.size(); ++index) {
        CHECK(loaded.value.prepared_actions[index].target == record.prepared_actions[index].target);
        CHECK(loaded.value.prepared_actions[index].before == record.prepared_actions[index].before);
        CHECK(loaded.value.prepared_actions[index].proposed == record.prepared_actions[index].proposed);
        CHECK(loaded.value.outcomes[index].state == record.outcomes[index].state);
    }
}

TEST_CASE("JSON backups round-trip rollback crash bookkeeping") {
    TemporaryBackupRoot root("gno-json-backup-crash-round-trip");
    JsonBackupStore store(root.path());
    auto record = persistedRecord(transactionId(1299));
    record.status = TransactionStatus::RollbackFailed;
    revertOutcome(record.outcomes[7]);
    auto& failed = record.outcomes[6];
    failed.rollback_attempted = true;
    failed.rollback_error = RemediationError::VerificationMismatch;
    failed.rollback_detail = "rollback observation differed";
    record.action_order.clear();
    record.error = RemediationError::RollbackFailed;
    record.detail = failed.rollback_detail;

    REQUIRE(store.save(record).ok());
    const auto loaded = store.load(record.transaction_id);

    REQUIRE(loaded.ok());
    CHECK(loaded.value.error == record.error);
    CHECK(loaded.value.detail == record.detail);
    CHECK(loaded.value.action_order == record.action_order);
    CHECK(loaded.value.applied_action_order == record.applied_action_order);
    REQUIRE(loaded.value.outcomes.size() == record.outcomes.size());
    for (std::size_t index = 0; index < record.outcomes.size(); ++index) {
        const auto& actual = loaded.value.outcomes[index];
        const auto& expected = record.outcomes[index];
        CHECK(actual.id == expected.id);
        CHECK(actual.status == expected.status);
        CHECK(actual.error == expected.error);
        CHECK(actual.attempted == expected.attempted);
        CHECK(actual.state == expected.state);
        CHECK(actual.detail == expected.detail);
        CHECK(actual.rollback_error == expected.rollback_error);
        CHECK(actual.rollback_attempted == expected.rollback_attempted);
        CHECK(actual.rollback_detail == expected.rollback_detail);
    }
}

TEST_CASE("JSON backups accept every durable transaction engine state") {
    TemporaryBackupRoot root("gno-json-backup-legal-states");
    JsonBackupStore store(root.path());
    std::vector<TransactionRecord> records;

    records.push_back(recordWithAppliedPrefix(
        transactionId(1300), 0, TransactionStatus::Prepared));
    records.push_back(recordWithAppliedPrefix(
        transactionId(1301), 2, TransactionStatus::Applying));
    records.push_back(persistedRecord(transactionId(1302), TransactionStatus::Applied));

    auto failed = recordWithAppliedPrefix(
        transactionId(1303), 2, TransactionStatus::Failed);
    auto& failed_outcome = failed.outcomes[2];
    failed_outcome.status = ActionStatus::Failed;
    failed_outcome.error = RemediationError::ApplyFailed;
    failed_outcome.attempted = true;
    failed_outcome.detail = "apply failed";
    failed.error = RemediationError::ApplyFailed;
    failed.detail = failed_outcome.detail;
    records.push_back(std::move(failed));

    auto backup_failed = recordWithAppliedPrefix(
        transactionId(1304), 2, TransactionStatus::Failed);
    backup_failed.error = RemediationError::BackupFailed;
    backup_failed.detail = "verified transition was not durably acknowledged";
    records.push_back(std::move(backup_failed));

    auto cancelled = recordWithAppliedPrefix(
        transactionId(1305), 2, TransactionStatus::Cancelled);
    cancelled.error = RemediationError::Cancelled;
    cancelled.detail = "execution cancelled";
    records.push_back(std::move(cancelled));

    auto rollback_cancelled = persistedRecord(transactionId(1310));
    rollback_cancelled.status = TransactionStatus::Cancelled;
    revertOutcome(rollback_cancelled.outcomes[7]);
    rollback_cancelled.action_order = {ActionId::ProcessPriority};
    rollback_cancelled.error = RemediationError::Cancelled;
    rollback_cancelled.detail = "rollback cancelled";
    records.push_back(std::move(rollback_cancelled));

    auto rolling_back = persistedRecord(transactionId(1306));
    rolling_back.status = TransactionStatus::RollingBack;
    rolling_back.action_order.clear();
    records.push_back(std::move(rolling_back));

    auto resumed_rollback = persistedRecord(transactionId(1307));
    resumed_rollback.status = TransactionStatus::RollingBack;
    revertOutcome(resumed_rollback.outcomes[7]);
    revertOutcome(resumed_rollback.outcomes[6]);
    resumed_rollback.action_order = {ActionId::Mtu};
    records.push_back(std::move(resumed_rollback));

    auto rollback_failed = persistedRecord(transactionId(1308));
    rollback_failed.status = TransactionStatus::RollbackFailed;
    revertOutcome(rollback_failed.outcomes[7]);
    revertOutcome(rollback_failed.outcomes[6]);
    auto& unresolved = rollback_failed.outcomes[5];
    unresolved.rollback_attempted = true;
    unresolved.rollback_error = RemediationError::VerificationMismatch;
    unresolved.rollback_detail = "rollback verification failed";
    rollback_failed.action_order = {ActionId::Mtu};
    rollback_failed.error = RemediationError::RollbackFailed;
    rollback_failed.detail = unresolved.rollback_detail;
    records.push_back(std::move(rollback_failed));

    records.push_back(persistedRecord(
        transactionId(1309), TransactionStatus::Reverted));

    for (const auto& record : records) {
        CAPTURE(record.transaction_id);
        REQUIRE(store.save(record).ok());
        const auto loaded = store.load(record.transaction_id);
        REQUIRE(loaded.ok());
        CHECK(loaded.value.status == record.status);
    }
}

TEST_CASE("JSON backups reject impossible transaction state-machine bookkeeping") {
    TemporaryBackupRoot root("gno-json-backup-invalid-states");
    JsonBackupStore store(root.path());
    unsigned int next_id = 1400;
    const auto rejected = [&](TransactionRecord record) {
        record.transaction_id = transactionId(next_id++);
        CAPTURE(record.transaction_id);
        CHECK_FALSE(store.save(record).ok());
    };

    auto empty_applied = persistedRecord(transactionId(0));
    empty_applied.prepared_actions.clear();
    empty_applied.outcomes.clear();
    empty_applied.action_order.clear();
    empty_applied.applied_action_order.clear();
    rejected(std::move(empty_applied));

    auto duplicate_order = persistedRecord(transactionId(0));
    duplicate_order.action_order.back() = duplicate_order.action_order.front();
    rejected(std::move(duplicate_order));

    auto non_prefix_order = persistedRecord(transactionId(0));
    std::swap(non_prefix_order.applied_action_order[0],
              non_prefix_order.applied_action_order[1]);
    rejected(std::move(non_prefix_order));

    auto misaligned_outcome = persistedRecord(transactionId(0));
    misaligned_outcome.outcomes[0].id = ActionId::Dns;
    rejected(std::move(misaligned_outcome));

    auto prepared_with_history = persistedRecord(transactionId(0));
    prepared_with_history.status = TransactionStatus::Prepared;
    rejected(std::move(prepared_with_history));

    auto applying_complete = persistedRecord(transactionId(0));
    applying_complete.status = TransactionStatus::Applying;
    rejected(std::move(applying_complete));

    auto applied_partial = recordWithAppliedPrefix(
        transactionId(0), 2, TransactionStatus::Applied);
    rejected(std::move(applied_partial));

    auto unattempted_applied = persistedRecord(transactionId(0));
    unattempted_applied.outcomes[0].attempted = false;
    rejected(std::move(unattempted_applied));

    auto attempted_not_checked = recordWithAppliedPrefix(
        transactionId(0), 0, TransactionStatus::Prepared);
    attempted_not_checked.outcomes[0].attempted = true;
    rejected(std::move(attempted_not_checked));

    auto successful_with_error = persistedRecord(transactionId(0));
    successful_with_error.outcomes[0].error = RemediationError::ApplyFailed;
    rejected(std::move(successful_with_error));

    auto detail_without_error = persistedRecord(transactionId(0));
    detail_without_error.outcomes[0].detail = "impossible detail";
    rejected(std::move(detail_without_error));

    auto failed_with_fabricated_empty_state = recordWithAppliedPrefix(
        transactionId(0), 0, TransactionStatus::Failed);
    auto& fabricated_failure = failed_with_fabricated_empty_state.outcomes[0];
    fabricated_failure.status = ActionStatus::Failed;
    fabricated_failure.attempted = true;
    fabricated_failure.error = RemediationError::ApplyFailed;
    fabricated_failure.detail = "apply failed";
    fabricated_failure.state.status = ActionStatus::Recommended;
    fabricated_failure.state.detail = "state was never observed";
    failed_with_fabricated_empty_state.error = fabricated_failure.error;
    failed_with_fabricated_empty_state.detail = fabricated_failure.detail;
    rejected(std::move(failed_with_fabricated_empty_state));

    auto rollback_error_without_attempt = persistedRecord(transactionId(0));
    rollback_error_without_attempt.outcomes[0].rollback_error =
        RemediationError::RollbackFailed;
    rejected(std::move(rollback_error_without_attempt));

    auto reverted_without_rollback = persistedRecord(
        transactionId(0), TransactionStatus::Reverted);
    reverted_without_rollback.outcomes[0].rollback_attempted = false;
    rejected(std::move(reverted_without_rollback));

    auto successful_record_with_error = persistedRecord(transactionId(0));
    successful_record_with_error.error = RemediationError::ApplyFailed;
    rejected(std::move(successful_record_with_error));

    auto failed_without_error = recordWithAppliedPrefix(
        transactionId(0), 2, TransactionStatus::Failed);
    rejected(std::move(failed_without_error));

    auto failed_error_mismatch = recordWithAppliedPrefix(
        transactionId(0), 1, TransactionStatus::Failed);
    failed_error_mismatch.outcomes[1].status = ActionStatus::Failed;
    failed_error_mismatch.outcomes[1].attempted = true;
    failed_error_mismatch.outcomes[1].error = RemediationError::ApplyFailed;
    failed_error_mismatch.error = RemediationError::PermissionDenied;
    rejected(std::move(failed_error_mismatch));

    auto resolved_rollback_failure = persistedRecord(
        transactionId(0), TransactionStatus::Reverted);
    resolved_rollback_failure.status = TransactionStatus::RollbackFailed;
    resolved_rollback_failure.error = RemediationError::RollbackFailed;
    resolved_rollback_failure.detail = "cannot fail resolved rollback";
    rejected(std::move(resolved_rollback_failure));

    auto reverted_with_unresolved = persistedRecord(
        transactionId(0), TransactionStatus::Reverted);
    reverted_with_unresolved.outcomes[0].status = ActionStatus::Applied;
    reverted_with_unresolved.outcomes[0].rollback_attempted = false;
    rejected(std::move(reverted_with_unresolved));

    auto rollback_with_apply_order = persistedRecord(transactionId(0));
    rollback_with_apply_order.status = TransactionStatus::RollingBack;
    rejected(std::move(rollback_with_apply_order));

    auto unknown_outcome_status = persistedRecord(transactionId(0));
    unknown_outcome_status.outcomes[0].status = ActionStatus::Recommended;
    rejected(std::move(unknown_outcome_status));
}

TEST_CASE("JSON backup load and list share state-machine validation") {
    TemporaryBackupRoot root("gno-json-backup-shared-validation");
    JsonBackupStore store(root.path());
    const auto invalid_id = transactionId(1500);
    const auto valid_id = transactionId(1501);
    REQUIRE(store.save(persistedRecord(invalid_id)).ok());
    REQUIRE(store.save(recordWithAppliedPrefix(
        valid_id, 0, TransactionStatus::Prepared)).ok());

    auto document = nlohmann::json::parse(readText(backupPath(root, invalid_id)));
    document["transaction"]["action_order"][1] =
        document["transaction"]["action_order"][0];
    writeText(backupPath(root, invalid_id), document.dump());

    CHECK_FALSE(store.load(invalid_id).ok());
    const auto summaries = store.list();
    REQUIRE(summaries.ok());
    REQUIRE(summaries.value.size() == 1);
    CHECK(summaries.value.front().transaction_id == valid_id);
    CHECK(summaries.value.front().status == TransactionStatus::Prepared);
}

TEST_CASE("JSON backups reject unsafe identifiers and files beyond the transaction bound") {
    TemporaryBackupRoot root("gno-json-backup-bounds");
    JsonBackupStore store(root.path());
    auto invalid = persistedRecord("not-a-uuid");

    CHECK_FALSE(store.save(invalid).ok());
    CHECK_FALSE(store.load("../escape").ok());

    auto invalid_status = persistedRecord(transactionId(7));
    invalid_status.status = static_cast<TransactionStatus>(99);
    CHECK_FALSE(store.save(invalid_status).ok());

    auto unsafe_proposal = persistedRecord(transactionId(8));
    unsafe_proposal.prepared_actions.front().proposed.value = PriorityValue::Realtime;
    CHECK_FALSE(store.save(unsafe_proposal).ok());

    auto incompatible = persistedRecord(transactionId(9));
    incompatible.prepared_actions.front().target = InterfaceId{"unexpected-interface"};
    CHECK_FALSE(store.save(incompatible).ok());

    const auto id = transactionId(2);
    const auto path = backupPath(root, id);
    writeText(path, std::string(256 * 1024 + 1, 'x'));
    CHECK_FALSE(store.load(id).ok());
}

TEST_CASE("JSON backups leave malformed future and foreign records intact") {
    TemporaryBackupRoot root("gno-json-backup-untrusted");
    JsonBackupStore store(root.path());

    const auto malformed_id = transactionId(3);
    const auto malformed_path = backupPath(root, malformed_id);
    const std::string malformed = "{not json";
    writeText(malformed_path, malformed);
    CHECK_FALSE(store.load(malformed_id).ok());
    CHECK(readText(malformed_path) == malformed);
    CHECK_FALSE(store.save(persistedRecord(malformed_id)).ok());
    CHECK(readText(malformed_path) == malformed);

    const auto future_id = transactionId(4);
    const auto future_path = backupPath(root, future_id);
    const std::string future =
        "{\"version\":2,\"producer\":\"E2E4 Soft\",\"transaction\":{}}";
    writeText(future_path, future);
    CHECK_FALSE(store.load(future_id).ok());
    CHECK(readText(future_path) == future);
    CHECK_FALSE(store.save(persistedRecord(future_id)).ok());
    CHECK(readText(future_path) == future);

    const auto foreign_id = transactionId(5);
    const auto foreign_path = backupPath(root, foreign_id);
    const std::string foreign =
        "{\"version\":1,\"producer\":\"Other application\",\"transaction\":{}}";
    writeText(foreign_path, foreign);
    CHECK_FALSE(store.load(foreign_id).ok());
    CHECK(readText(foreign_path) == foreign);
    CHECK_FALSE(store.save(persistedRecord(foreign_id)).ok());
    CHECK(readText(foreign_path) == foreign);
}

TEST_CASE("JSON backup save preserves the previous record when temporary output fails") {
    TemporaryBackupRoot root("gno-json-backup-atomic");
    JsonBackupStore store(root.path());
    const auto id = transactionId(6);
    const auto original = persistedRecord(id);
    REQUIRE(store.save(original).ok());
    const auto path = backupPath(root, id);
    const auto valid_before_failed_save = readText(path);

    const auto temporary = path.string() + ".tmp";
    std::filesystem::create_directory(temporary);
    auto changed = original;
    changed.detail = "must not replace prior transaction";
    CHECK_FALSE(store.save(changed).ok());
    CHECK(readText(path) == valid_before_failed_save);
}

TEST_CASE("JSON backup rejects replacement that changes an unresolved rollback plan") {
    TemporaryBackupRoot root("gno-json-backup-collision");
    JsonBackupStore store(root.path());
    auto original = persistedRecord(transactionId(10));
    REQUIRE(store.save(original).ok());
    auto replacement = original;
    replacement.prepared_actions.front().before.value =
        PowerPlanValue{"different-original-plan"};
    CHECK_FALSE(store.save(replacement).ok());
}

TEST_CASE("JSON backup retention keeps unresolved records and bounds only resolved records") {
    TemporaryBackupRoot root("gno-json-backup-retention");
    JsonBackupStore store(root.path());
    const auto unresolved_id = transactionId(1000);
    auto unresolved = recordWithAppliedPrefix(
        unresolved_id, 0, TransactionStatus::Prepared);
    REQUIRE(store.save(unresolved).ok());

    const auto timestamp = std::filesystem::file_time_type::clock::now() -
                           std::chrono::hours(1);
    for (unsigned int index = 1; index <= 100; ++index) {
        const auto id = transactionId(1000 + index);
        REQUIRE(store.save(persistedRecord(id, TransactionStatus::Reverted)).ok());
        std::error_code error;
        std::filesystem::last_write_time(
            backupPath(root, id), timestamp + std::chrono::seconds(index), error);
        REQUIRE_FALSE(error);
    }
    REQUIRE(store.save(persistedRecord(
        transactionId(1101), TransactionStatus::Reverted)).ok());

    CHECK(store.load(unresolved_id).ok());
    CHECK_FALSE(store.load(transactionId(1001)).ok());
    CHECK(store.load(transactionId(1101)).ok());
    const auto summaries = store.list();
    REQUIRE(summaries.ok());
    CHECK(summaries.value.size() == 101);
}

TEST_CASE("JSON backup reports deterministic retention deletion failure after durable save") {
    TemporaryBackupRoot root("gno-json-backup-retention-failure");
    JsonBackupStore store(root.path());
    const auto unresolved_id = transactionId(2000);
    REQUIRE(store.save(recordWithAppliedPrefix(
        unresolved_id, 0, TransactionStatus::Prepared)).ok());

    const auto timestamp = std::filesystem::file_time_type::clock::now() -
                           std::chrono::hours(1);
    for (unsigned int index = 1; index <= 100; ++index) {
        const auto id = transactionId(2000 + index);
        REQUIRE(store.save(persistedRecord(id, TransactionStatus::Reverted)).ok());
        std::error_code error;
        std::filesystem::last_write_time(
            backupPath(root, id), timestamp + std::chrono::seconds(index), error);
        REQUIRE_FALSE(error);
    }

    std::vector<std::filesystem::path> removal_attempts;
    JsonBackupStore failing_store(
        root.path(),
        [&](const std::filesystem::path& path, std::error_code& error) {
            removal_attempts.push_back(path);
            error = std::make_error_code(std::errc::permission_denied);
            return false;
        });
    const auto newest_id = transactionId(2101);

    const auto saved = failing_store.save(
        persistedRecord(newest_id, TransactionStatus::Reverted));

    CHECK_FALSE(saved.ok());
    REQUIRE(removal_attempts.size() == 1);
    CHECK(removal_attempts.front() == backupPath(root, transactionId(2001)));
    CHECK(failing_store.load(newest_id).ok());
    CHECK(failing_store.load(transactionId(2001)).ok());
    CHECK(failing_store.load(unresolved_id).ok());
    const auto summaries = failing_store.list();
    REQUIRE(summaries.ok());
    CHECK(summaries.value.size() == 102);
    CHECK(std::count_if(
              summaries.value.begin(), summaries.value.end(),
              [](const TransactionSummary& summary) {
                  return summary.status == TransactionStatus::Reverted;
              }) == 101);
}

#ifndef _WIN32
TEST_CASE("JSON backups reject symlinks and FIFO records without blocking") {
    TemporaryBackupRoot root("gno-json-backup-special-files");
    JsonBackupStore store(root.path());
    const auto external_id = transactionId(1200);
    REQUIRE(store.save(persistedRecord(external_id)).ok());
    const auto link_id = transactionId(1201);
    const auto link = backupPath(root, link_id);
    std::filesystem::create_directories(link.parent_path());
    std::filesystem::create_symlink(backupPath(root, external_id), link);
    CHECK_FALSE(store.load(link_id).ok());

    const auto fifo_id = transactionId(1202);
    const auto fifo = backupPath(root, fifo_id);
    REQUIRE(::mkfifo(fifo.c_str(), 0600) == 0);
    CHECK_FALSE(store.load(fifo_id).ok());
    const auto summaries = store.list();
    REQUIRE(summaries.ok());
    CHECK(summaries.value.size() == 1);
}
#endif

TEST_SUITE_END();
