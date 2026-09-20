#include "remediation/fix_transaction.h"
#include "remediation/remediation_service.h"   // summarizeValue для диагностики

#include <mutex>
#include <cstdio>

namespace gno {
namespace remediation {

namespace {
// Process-wide mutation lock: only one transaction may mutate state at a time.
std::mutex& mutationMutex() {
    static std::mutex m;
    return m;
}
} // namespace

FixTransaction::FixTransaction(std::string transaction_id,
                               std::vector<std::unique_ptr<FixAction>> actions,
                               std::vector<ActionTarget> targets,
                               IBackupStore& backup_store)
    : transaction_id_(std::move(transaction_id)),
      actions_(std::move(actions)),
      targets_(std::move(targets)),
      backup_store_(backup_store) {
    record_.transaction_id = transaction_id_;
    record_.schema_version = 1;
    record_.status = TransactionStatus::Unprepared;
    // Фиксируем время создания один раз: при откате запись сохраняется повторно,
    // и если бы время бралось из mtime файла, оно бы «обновлялось».
    record_.created_at = formatLocalTimestamp();
}

TransactionRecord FixTransaction::record() const {
    // Копирование под блокировкой: старая версия возвращала const&, мьютекс
    // отпускался при возврате, и вызывающий работал с record_ без защиты.
    // Худший случай — execute() → persist(record()): ссылка на record_
    // переживает запись файла в backup store, и параллельный setRecord()
    // мог разорвать JSON, который мы только что записали на диск.
    std::lock_guard<std::mutex> lock(state_mutex_);
    return record_;
}

void FixTransaction::setRecord(const TransactionRecord& record) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    record_ = record;
}

bool FixTransaction::restoreRecord(const TransactionRecord& persisted) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (record_.status != TransactionStatus::Unprepared) return false;
    record_ = persisted;
    return true;
}

SimpleResult FixTransaction::persist(const TransactionRecord& record) {
    return backup_store_.save(record);
}

Result<TransactionRecord> FixTransaction::busyResult() const {
    // Текст доходит до пользователя (BoostGate кладёт detail в результат, UI его
    // показывает), поэтому он на русском, как и остальные сообщения движка.
    return Fail(RemediationError::Busy,
                "операция над этой транзакцией уже выполняется — дождитесь завершения");
}

bool FixTransaction::rollbackEligible(const TransactionRecord& record) const {
    // Правило живёт в remediation_types.h: его же используют UI и BoostGate,
    // чтобы кнопка отката и движок не разъезжались.
    return remediation::isRollbackEligible(record);
}

Result<TransactionRecord> FixTransaction::prepare() {
    if (!operation_mutex_.try_lock()) return busyResult();

    TransactionRecord current = record();
    if (current.status != TransactionStatus::Unprepared) {
        operation_mutex_.unlock();
        return Fail(RemediationError::PreflightFailed, "transaction preparation is one-shot");
    }

    if (actions_.size() > kMaxTransactionActions || actions_.size() != targets_.size()) {
        operation_mutex_.unlock();
        return Fail(RemediationError::InternalFailure, "action/target count mismatch");
    }

    current.prepared_actions.clear();
    current.outcomes.clear();
    current.action_order.clear();
    current.applied_action_order.clear();
    current.error = RemediationError::None;
    current.detail.clear();

    for (std::size_t i = 0; i < actions_.size(); ++i) {
        auto observed = actions_[i]->observe(targets_[i]);
        if (!observed) {
            current.status = TransactionStatus::Failed;
            current.error = observed.code();
            current.detail = observed.detail();
            setRecord(current);
            operation_mutex_.unlock();
            return Result<TransactionRecord>(observed.error());
        }
        if (!isValidState(observed.value())) {
            operation_mutex_.unlock();
            // Без имени действия непонятно, что именно не прошло проверку:
            // одна «невозможная» величина в системе рушила всю транзакцию.
            return Fail(RemediationError::PreflightFailed,
                        "observed state out of bounds for " +
                            to_string(actions_[i]->id()) + ": " +
                            summarizeValue(observed.value().value));
        }

        auto prepared = actions_[i]->prepare(targets_[i], observed.value());
        if (!prepared) {
            current.status = TransactionStatus::Failed;
            current.error = prepared.code();
            current.detail = prepared.detail();
            setRecord(current);
            operation_mutex_.unlock();
            return Result<TransactionRecord>(prepared.error());
        }

        current.prepared_actions.push_back(prepared.value());

        ActionOutcome outcome;
        outcome.id = actions_[i]->id();
        outcome.status = ActionStatus::NotChecked;
        outcome.state = observed.value();
        current.outcomes.push_back(outcome);
    }

    current.status = TransactionStatus::Prepared;
    setRecord(current);

    auto saved = persist(record());
    if (!saved) {
        TransactionRecord failed = record();
        failed.status = TransactionStatus::Failed;
        failed.error = RemediationError::BackupFailed;
        failed.detail = saved.detail();
        setRecord(failed);
        operation_mutex_.unlock();
        return Fail(RemediationError::BackupFailed, saved.detail());
    }

    operation_mutex_.unlock();
    return record();
}

Result<TransactionRecord> FixTransaction::execute() {
    std::unique_lock<std::mutex> operation(operation_mutex_, std::try_to_lock);
    if (!operation.owns_lock()) return busyResult();
    std::unique_lock<std::mutex> mutation(mutationMutex(), std::try_to_lock);
    if (!mutation.owns_lock()) return busyResult();

    TransactionRecord current = record();
    if (current.status != TransactionStatus::Prepared)
        return Fail(RemediationError::PreflightFailed, "transaction is not prepared");
    if (current.prepared_actions.size() != actions_.size())
        return Fail(RemediationError::InternalFailure, "prepared action count mismatch");

    // Freshness check: the live state must still match what prepare() observed.
    for (std::size_t i = 0; i < actions_.size(); ++i) {
        auto fresh = actions_[i]->observe(targets_[i]);
        if (!fresh || !(fresh.value() == current.prepared_actions[i].before) ||
            !isValidState(fresh.value())) {
            return Fail(RemediationError::PreflightFailed,
                        fresh ? "prepared state is stale" : fresh.detail());
        }
    }

    auto prepared_save = persist(current);
    if (!prepared_save)
        return Fail(RemediationError::BackupFailed, prepared_save.detail());

    // Счётчики для отчёта: сколько действий реально не применилось и почему
    // (первая причина уходит в detail, остальные видны в outcomes).
    int failed_count = 0;
    RemediationError first_error = RemediationError::None;
    std::string first_error_detail;

    for (std::size_t i = 0; i < actions_.size(); ++i) {
        auto applied = actions_[i]->apply(current.prepared_actions[i]);
        ActionOutcome outcome = current.outcomes[i];
        outcome.attempted = true;

        // Одно действие, которое не применилось (например, MTU без прав
        // администратора), не должно отменять остальные. Раньше execute()
        // выходил на первой ошибке: все следующие пункты оставались
        // «не проверено», UI показывал их как «не применилось», хотя их даже
        // не пытались применить. Применённый префикс по-прежнему откатывается
        // (rollbackEligible принимает status=Applied с непустым
        // applied_action_order).
        auto fail_this = [&](RemediationError code, const std::string& detail) {
            outcome.status = ActionStatus::Failed;
            outcome.error = code;
            outcome.detail = detail;
            current.outcomes[i] = outcome;
            ++failed_count;
            if (first_error == RemediationError::None) {
                first_error = code;
                first_error_detail = detail;
            }
        };

        if (!applied) {
            fail_this(applied.code(), applied.detail());
            setRecord(current);
            persist(current);
            continue;
        }

        auto verified = actions_[i]->observe(targets_[i]);
        if (!verified) {
            fail_this(verified.code(), verified.detail());
            setRecord(current);
            persist(current);
            continue;
        }

        if (!(verified.value() == current.prepared_actions[i].proposed) ||
            !isValidState(verified.value())) {
            outcome.state = verified.value();
            fail_this(RemediationError::VerificationMismatch,
                      "post-apply state did not match the prepared value");
            setRecord(current);
            persist(current);
            continue;
        }

        outcome.status = ActionStatus::Applied;
        outcome.error = RemediationError::None;
        outcome.state = verified.value();
        outcome.detail.clear();
        current.outcomes[i] = outcome;
        current.applied_action_order.push_back(actions_[i]->id());
        current.status = TransactionStatus::Applying;
        current.error = RemediationError::None;
        setRecord(current);

        auto saved = persist(record());
        if (!saved) {
            TransactionRecord failed = record();
            failed.status = TransactionStatus::Failed;
            failed.error = RemediationError::BackupFailed;
            failed.detail = saved.detail();
            setRecord(failed);
            return Fail(RemediationError::BackupFailed, saved.detail());
        }
    }

    if (failed_count > 0) {
        // Статус записи = Applied, если хоть что-то применилось: журнал должен
        // остаться откатываемым. Ошибку отдаём через Result, чтобы вызывающий
        // отчитался по каждому действию отдельно.
        current.status = current.applied_action_order.empty() ? TransactionStatus::Failed
                                                             : TransactionStatus::Applied;
        current.error = first_error;
        current.detail = first_error_detail;
        setRecord(current);
        persist(current);
        return Result<TransactionRecord>(Error{first_error, first_error_detail});
    }

    current.status = TransactionStatus::Applied;
    current.error = RemediationError::None;
    current.detail.clear();
    setRecord(current);
    persist(current);
    return record();
}

Result<TransactionRecord> FixTransaction::rollback() {
    std::unique_lock<std::mutex> operation(operation_mutex_, std::try_to_lock);
    if (!operation.owns_lock()) return busyResult();
    std::unique_lock<std::mutex> mutation(mutationMutex(), std::try_to_lock);
    if (!mutation.owns_lock()) return busyResult();

    TransactionRecord current = record();
    if (current.prepared_actions.size() != actions_.size() ||
        current.outcomes.size() != actions_.size() || !rollbackEligible(current))
        return Fail(RemediationError::RollbackFailed,
                    "transaction has no eligible reversible applied prefix");

    // Freshness: applied states must be unchanged since execution.
    for (ActionId applied_id : current.applied_action_order) {
        for (std::size_t i = 0; i < current.outcomes.size(); ++i) {
            if (current.outcomes[i].id != applied_id || current.outcomes[i].status != ActionStatus::Applied)
                continue;
            auto fresh = actions_[i]->observe(targets_[i]);
            if (!fresh || !(fresh.value() == current.outcomes[i].state))
                return Fail(RemediationError::RollbackFailed,
                            fresh ? "applied state changed before rollback" : fresh.detail());
        }
    }

    current.status = TransactionStatus::RollingBack;
    current.error = RemediationError::None;
    current.detail.clear();
    current.action_order.clear();
    setRecord(current);
    auto rolling_save = persist(record());
    if (!rolling_save)
        return Fail(RemediationError::BackupFailed, rolling_save.detail());

    bool any_failure = false;
    RemediationError failure_code = RemediationError::None;
    std::string failure_detail;

    for (std::size_t reverse = current.applied_action_order.size(); reverse > 0; --reverse) {
        const ActionId applied_id = current.applied_action_order[reverse - 1];
        for (std::size_t i = 0; i < actions_.size(); ++i) {
            if (current.outcomes[i].id != applied_id || current.outcomes[i].status != ActionStatus::Applied)
                continue;

            auto rolled_back = actions_[i]->rollback(current.prepared_actions[i]);
            ActionOutcome outcome = current.outcomes[i];
            outcome.rollback_attempted = true;

            if (!rolled_back) {
                outcome.rollback_error = rolled_back.code();
                outcome.rollback_detail = rolled_back.detail();
                outcome.status = ActionStatus::Failed;
                any_failure = true;
                failure_code = rolled_back.code();
                failure_detail = rolled_back.detail();
            } else if (!(rolled_back.value() == current.prepared_actions[i].before)) {
                outcome.rollback_error = RemediationError::VerificationMismatch;
                outcome.rollback_detail = "rollback verification mismatch";
                outcome.status = ActionStatus::Failed;
                any_failure = true;
                failure_code = RemediationError::VerificationMismatch;
                failure_detail = outcome.rollback_detail;
            } else {
                outcome.status = ActionStatus::Reverted;
                outcome.state = rolled_back.value();
                outcome.rollback_error = RemediationError::None;
                outcome.rollback_detail.clear();
            }

            current.outcomes[i] = outcome;
            setRecord(current);
            auto saved = persist(record());
            if (!saved) {
                TransactionRecord failed = record();
                failed.status = TransactionStatus::RollbackFailed;
                failed.error = RemediationError::BackupFailed;
                failed.detail = saved.detail();
                setRecord(failed);
                return Fail(RemediationError::BackupFailed, saved.detail());
            }
        }
    }

    current = record();
    current.status = any_failure ? TransactionStatus::RollbackFailed : TransactionStatus::Reverted;
    current.error = any_failure ? failure_code : RemediationError::None;
    current.detail = any_failure ? failure_detail : std::string("rollback completed");
    setRecord(current);
    persist(record());

    if (any_failure)
        return Result<TransactionRecord>(Error{failure_code, failure_detail});
    return record();
}

} // namespace remediation
} // namespace gno
