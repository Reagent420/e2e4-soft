#include "remediation/fix_transaction.h"

#include <algorithm>
#include <array>
#include <utility>

namespace gno {
namespace {

std::mutex& mutationMutex() {
    static std::mutex mutex;
    return mutex;
}

std::string boundedDetail(std::string detail) {
    if (detail.size() > kMaxDetailLength) {
        detail.resize(kMaxDetailLength);
    }
    return detail;
}

template <typename T>
Result<T> makeResult(T value, RemediationError error, std::string detail = {}) {
    Result<T> result;
    result.value = std::move(value);
    result.error = error;
    result.detail = boundedDetail(std::move(detail));
    return result;
}

bool isBounded(const ActionState& state) noexcept {
    return state.detail.size() <= kMaxDetailLength && isBounded(state.value);
}

bool isBounded(const PreparedAction& action) noexcept {
    return isBounded(action.target) && isBounded(action.before) &&
           isBounded(action.proposed);
}

bool matchingState(const ActionState& state, const ActionState& expected) noexcept {
    return state.id == expected.id && isBounded(state) && state.value == expected.value;
}

} // namespace

FixTransaction::FixTransaction(
    std::string transaction_id,
    std::vector<std::unique_ptr<FixAction>> actions,
    std::vector<ActionTarget> targets,
    BackupStore& backup_store)
    : transaction_id_(std::move(transaction_id)),
      actions_(std::move(actions)),
      targets_(std::move(targets)),
      backup_store_(backup_store) {
    record_.transaction_id = transaction_id_;
}

Result<TransactionRecord> FixTransaction::prepare(
    const CancellationToken& cancellation) {
    std::unique_lock<std::mutex> operation_lock(operation_mutex_, std::try_to_lock);
    if (!operation_lock.owns_lock()) {
        return busyResult();
    }

    TransactionRecord prepared;
    prepared.transaction_id = transaction_id_;

    if (transaction_id_.empty() || transaction_id_.size() > kMaxTransactionIdLength ||
        actions_.empty() || actions_.size() > kMaxTransactionActions ||
        actions_.size() != targets_.size()) {
        return fail(std::move(prepared), RemediationError::InvalidTarget,
                    "invalid transaction shape", false);
    }

    std::array<bool, kMaxTransactionActions> seen{};
    for (std::size_t index = 0; index < actions_.size(); ++index) {
        if (!actions_[index] || !isBounded(targets_[index])) {
            return fail(std::move(prepared), RemediationError::InvalidTarget,
                        "invalid action target", false);
        }

        const auto id_index = static_cast<std::size_t>(actions_[index]->id());
        if (id_index >= seen.size() || seen[id_index]) {
            return fail(std::move(prepared), RemediationError::PreflightFailed,
                        "duplicate or unknown action", false);
        }
        seen[id_index] = true;

        if (cancellation.isCancelled()) {
            return fail(std::move(prepared), RemediationError::Cancelled,
                        "preflight cancelled", false);
        }

        const auto observed = actions_[index]->observe(targets_[index], cancellation);
        if (!observed.ok()) {
            return fail(std::move(prepared), observed.error, observed.detail, false);
        }
        if (observed.value.id != actions_[index]->id() || !isBounded(observed.value)) {
            return fail(std::move(prepared), RemediationError::PreflightFailed,
                        "invalid observed state", false);
        }

        const auto action = actions_[index]->prepare(targets_[index], observed.value);
        if (!action.ok()) {
            return fail(std::move(prepared), action.error, action.detail, false);
        }
        if (action.value.id != actions_[index]->id() ||
            action.value.before.id != actions_[index]->id() ||
            action.value.proposed.id != actions_[index]->id() ||
            action.value.target != targets_[index] || !isBounded(action.value)) {
            return fail(std::move(prepared), RemediationError::PreflightFailed,
                        "invalid prepared action", false);
        }

        prepared.prepared_actions.push_back(action.value);
        ActionOutcome outcome;
        outcome.id = actions_[index]->id();
        outcome.state.id = actions_[index]->id();
        prepared.outcomes.push_back(std::move(outcome));
    }

    prepared.status = TransactionStatus::Prepared;
    prepared.error = RemediationError::None;
    replaceRecord(prepared);
    return makeResult(std::move(prepared), RemediationError::None);
}

Result<TransactionRecord> FixTransaction::execute(
    const CancellationToken& cancellation) {
    std::unique_lock<std::mutex> operation_lock(operation_mutex_, std::try_to_lock);
    if (!operation_lock.owns_lock()) {
        return busyResult();
    }
    std::unique_lock<std::mutex> mutation_lock(mutationMutex(), std::try_to_lock);
    if (!mutation_lock.owns_lock()) {
        return busyResult();
    }

    TransactionRecord current = record();
    if (current.status != TransactionStatus::Prepared ||
        current.prepared_actions.size() != actions_.size() ||
        current.outcomes.size() != actions_.size()) {
        return makeResult(std::move(current), RemediationError::PreflightFailed,
                          "transaction is not prepared");
    }

    const auto prepared_save = persist(current);
    if (!prepared_save.ok()) {
        current.status = TransactionStatus::Failed;
        current.error = RemediationError::BackupFailed;
        current.detail = boundedDetail(prepared_save.detail);
        replaceRecord(current);
        return makeResult(std::move(current), RemediationError::BackupFailed,
                          prepared_save.detail);
    }

    for (std::size_t index = 0; index < actions_.size(); ++index) {
        if (cancellation.isCancelled()) {
            return fail(std::move(current), RemediationError::Cancelled,
                        "execution cancelled", true);
        }

        const auto applied = actions_[index]->apply(
            current.prepared_actions[index], cancellation);
        auto& outcome = current.outcomes[index];
        outcome.attempted = true;

        if (!applied.ok()) {
            outcome.status = ActionStatus::Failed;
            outcome.error = applied.error;
            outcome.detail = boundedDetail(applied.detail);
            return fail(std::move(current), applied.error, applied.detail, true);
        }

        if (!matchingState(applied.value, current.prepared_actions[index].proposed)) {
            outcome.status = ActionStatus::Failed;
            outcome.error = RemediationError::VerificationMismatch;
            outcome.detail = "post-apply state did not match the prepared value";
            return fail(std::move(current), RemediationError::VerificationMismatch,
                        outcome.detail, true);
        }

        outcome.status = ActionStatus::Applied;
        outcome.error = RemediationError::None;
        outcome.state = applied.value;
        outcome.detail.clear();
        current.action_order.push_back(actions_[index]->id());
        current.status = index + 1 == actions_.size()
                             ? TransactionStatus::Applied
                             : TransactionStatus::Applying;
        current.error = RemediationError::None;
        current.detail.clear();
        replaceRecord(current);

        const auto saved = persist(current);
        if (!saved.ok()) {
            current.status = TransactionStatus::Failed;
            current.error = RemediationError::BackupFailed;
            current.detail = boundedDetail(saved.detail);
            replaceRecord(current);
            return makeResult(std::move(current), RemediationError::BackupFailed,
                              saved.detail);
        }
    }

    return makeResult(std::move(current), RemediationError::None);
}

Result<TransactionRecord> FixTransaction::rollback(
    const CancellationToken& cancellation) {
    std::unique_lock<std::mutex> operation_lock(operation_mutex_, std::try_to_lock);
    if (!operation_lock.owns_lock()) {
        return busyResult();
    }
    std::unique_lock<std::mutex> mutation_lock(mutationMutex(), std::try_to_lock);
    if (!mutation_lock.owns_lock()) {
        return busyResult();
    }

    TransactionRecord current = record();
    if (current.prepared_actions.size() != actions_.size() ||
        current.outcomes.size() != actions_.size()) {
        return makeResult(std::move(current), RemediationError::RollbackFailed,
                          "transaction has no rollback plan");
    }

    current.status = TransactionStatus::RollingBack;
    current.error = RemediationError::None;
    current.detail.clear();
    current.action_order.clear();
    replaceRecord(current);

    for (std::size_t reverse = actions_.size(); reverse > 0; --reverse) {
        const std::size_t index = reverse - 1;
        if (current.outcomes[index].status != ActionStatus::Applied) {
            continue;
        }

        if (cancellation.isCancelled()) {
            return fail(std::move(current), RemediationError::Cancelled,
                        "rollback cancelled", true);
        }

        current.action_order.push_back(actions_[index]->id());
        const auto rolled_back = actions_[index]->rollback(
            current.prepared_actions[index], cancellation);
        auto& outcome = current.outcomes[index];
        if (!rolled_back.ok() ||
            !matchingState(rolled_back.value, current.prepared_actions[index].before)) {
            outcome.status = ActionStatus::Failed;
            outcome.error = RemediationError::RollbackFailed;
            outcome.attempted = true;
            outcome.detail = rolled_back.ok()
                                 ? "post-rollback state did not match the original value"
                                 : boundedDetail(rolled_back.detail);
            return fail(std::move(current), RemediationError::RollbackFailed,
                        outcome.detail, true);
        }

        outcome.status = ActionStatus::Reverted;
        outcome.error = RemediationError::None;
        outcome.attempted = true;
        outcome.state = rolled_back.value;
        outcome.detail.clear();

        const bool has_more = std::any_of(
            current.outcomes.begin(), current.outcomes.end(),
            [](const ActionOutcome& candidate) {
                return candidate.status == ActionStatus::Applied;
            });
        current.status = has_more ? TransactionStatus::RollingBack
                                  : TransactionStatus::Reverted;
        replaceRecord(current);

        const auto saved = persist(current);
        if (!saved.ok()) {
            current.status = TransactionStatus::RollbackFailed;
            current.error = RemediationError::BackupFailed;
            current.detail = boundedDetail(saved.detail);
            replaceRecord(current);
            return makeResult(std::move(current), RemediationError::BackupFailed,
                              saved.detail);
        }
    }

    if (current.status == TransactionStatus::RollingBack) {
        current.status = TransactionStatus::Reverted;
        replaceRecord(current);
        const auto saved = persist(current);
        if (!saved.ok()) {
            return makeResult(std::move(current), RemediationError::BackupFailed,
                              saved.detail);
        }
    }
    return makeResult(std::move(current), RemediationError::None);
}

TransactionRecord FixTransaction::record() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return record_;
}

Result<TransactionRecord> FixTransaction::busyResult() const {
    return makeResult(record(), RemediationError::Busy,
                      "another transaction operation is in progress");
}

Result<TransactionRecord> FixTransaction::fail(
    TransactionRecord record, RemediationError error, std::string detail,
    bool should_persist) {
    if (error == RemediationError::Cancelled) {
        record.status = TransactionStatus::Cancelled;
    } else if (error == RemediationError::RollbackFailed) {
        record.status = TransactionStatus::RollbackFailed;
    } else {
        record.status = TransactionStatus::Failed;
    }
    record.error = error;
    record.detail = boundedDetail(std::move(detail));
    replaceRecord(record);

    if (should_persist) {
        const auto saved = persist(record);
        if (!saved.ok()) {
            record.error = RemediationError::BackupFailed;
            record.detail = boundedDetail(saved.detail);
            replaceRecord(record);
            return makeResult(std::move(record), RemediationError::BackupFailed,
                              saved.detail);
        }
    }
    const auto result_detail = record.detail;
    return makeResult(std::move(record), error, result_detail);
}

Result<std::monostate> FixTransaction::persist(const TransactionRecord& record) {
    auto saved = backup_store_.save(record);
    saved.detail = boundedDetail(std::move(saved.detail));
    if (!saved.ok() && saved.error == RemediationError::None) {
        saved.error = RemediationError::BackupFailed;
    }
    return saved;
}

void FixTransaction::replaceRecord(const TransactionRecord& record) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    record_ = record;
}

} // namespace gno
