#pragma once

#include "remediation/backup_store.h"
#include "remediation/fix_action.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace gno {

class FixTransaction {
public:
    FixTransaction(std::string transaction_id,
                   std::vector<std::unique_ptr<FixAction>> actions,
                   std::vector<ActionTarget> targets,
                   BackupStore& backup_store);

    FixTransaction(const FixTransaction&) = delete;
    FixTransaction& operator=(const FixTransaction&) = delete;

    Result<TransactionRecord> prepare(const CancellationToken& cancellation);
    Result<TransactionRecord> execute(const CancellationToken& cancellation);
    Result<TransactionRecord> rollback(const CancellationToken& cancellation);
    TransactionRecord record() const;

private:
    Result<TransactionRecord> busyResult() const;
    Result<TransactionRecord> fail(
        TransactionRecord record, RemediationError error, std::string detail,
        bool persist);
    Result<std::monostate> persist(const TransactionRecord& record);
    void replaceRecord(const TransactionRecord& record);

    std::string transaction_id_;
    std::vector<std::unique_ptr<FixAction>> actions_;
    std::vector<ActionTarget> targets_;
    BackupStore& backup_store_;

    mutable std::mutex state_mutex_;
    std::mutex operation_mutex_;
    TransactionRecord record_;
};

} // namespace gno
