#include "doctest.h"
#include "remediation/backup_store.h"
#include "remediation/fix_action.h"
#include "remediation/fix_transaction.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
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
    FakeAction(ActionId action_id, std::vector<std::string>& log)
        : action_id_(action_id), log_(log) {}

    ActionId id() const noexcept override { return action_id_; }

    Result<ActionState> observe(
        const ActionTarget&, const CancellationToken&) const override {
        log_.push_back(std::string("observe:") + actionName(action_id_));
        if (observe_error != RemediationError::None) {
            return failure<ActionState>(observe_error, "observe failed");
        }
        return success(ActionState{action_id_, ActionStatus::Recommended,
                                   originalValue(action_id_), {}});
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
        prepared.proposed = ActionState{action_id_, ActionStatus::Recommended,
                                        proposedValue(action_id_), {}};
        prepared.rollback_supported = true;
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
        auto value = verification_mismatch ? originalValue(action_id_)
                                           : prepared.proposed.value;
        return success(ActionState{action_id_, ActionStatus::Applied,
                                   std::move(value), {}});
    }

    Result<ActionState> rollback(
        const PreparedAction& prepared, const CancellationToken&) override {
        log_.push_back(std::string("rollback:") + actionName(action_id_));
        if (rollback_error != RemediationError::None) {
            return failure<ActionState>(rollback_error, "rollback failed");
        }
        auto value = rollback_mismatch ? prepared.proposed.value : prepared.before.value;
        return success(ActionState{action_id_, ActionStatus::Reverted,
                                   std::move(value), {}});
    }

    RemediationError observe_error = RemediationError::None;
    RemediationError prepare_error = RemediationError::None;
    RemediationError apply_error = RemediationError::None;
    RemediationError rollback_error = RemediationError::None;
    bool verification_mismatch = false;
    bool rollback_mismatch = false;
    std::function<void()> on_apply;

private:
    ActionId action_id_;
    std::vector<std::string>& log_;
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
                                          "save", "apply:power", "apply:dns"});
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
                                          "observe:dns", "prepare:dns", "save"});
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
    power->verification_mismatch = true;
    auto transaction = makeTransaction(store, std::move(actions));
    const gno::CancellationToken token;
    requirePrepared(*transaction, token);

    const auto result = transaction->execute(token);

    CHECK_FALSE(result.ok());
    CHECK(result.error == gno::RemediationError::VerificationMismatch);
    CHECK(result.value.outcomes[0].error == gno::RemediationError::VerificationMismatch);
    CHECK(result.value.outcomes[1].status == gno::ActionStatus::NotChecked);
    CHECK(log.back() == "apply:power");
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
    CHECK(log.back() == "apply:power");
}

TEST_CASE("rollback visits only verified applied actions in reverse order") {
    std::vector<std::string> log;
    FakeBackupStore store(log);
    auto transaction = makeTransaction(store, twoActions(log));
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
    CHECK(log[log.size() - 2] == "rollback:dns");
    CHECK(log.back() == "rollback:power");
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
