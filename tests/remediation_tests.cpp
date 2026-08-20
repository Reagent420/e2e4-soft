#include "doctest.h"
#include "remediation/backup_store.h"
#include "remediation/fix_action.h"
#include "remediation/fix_transaction.h"

#include <atomic>
#include <condition_variable>
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
        if (records.empty()) {
            log_.push_back("save");
        }
        records.push_back(record);
        if (fail_next_save) {
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

TEST_SUITE_END();
