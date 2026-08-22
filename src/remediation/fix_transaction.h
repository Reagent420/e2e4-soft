#pragma once

#include "remediation/backup_store.h"
#include "remediation/fix_action.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace gno {
namespace remediation {

// Transactional remediation engine: prepare -> execute -> rollback.
// Every mutation is preceded by a durable backup and followed by verification.
class FixTransaction {
public:
    FixTransaction(std::string transaction_id,
                   std::vector<std::unique_ptr<FixAction>> actions,
                   std::vector<ActionTarget> targets,
                   IBackupStore& backup_store);

    const TransactionRecord& record() const;

    Result<TransactionRecord> prepare();
    Result<TransactionRecord> execute();
    Result<TransactionRecord> rollback();

    // Loads a previously persisted record so rollback() can revert it.
    bool restoreRecord(const TransactionRecord& persisted);

private:
    SimpleResult persist(const TransactionRecord& record);
    void setRecord(const TransactionRecord& record);
    Result<TransactionRecord> busyResult() const;
    bool rollbackEligible(const TransactionRecord& record) const;

    std::string transaction_id_;
    std::vector<std::unique_ptr<FixAction>> actions_;
    std::vector<ActionTarget> targets_;
    IBackupStore& backup_store_;

    mutable std::mutex state_mutex_;
    std::mutex operation_mutex_;
    TransactionRecord record_;
};

} // namespace remediation
} // namespace gno
