#pragma once

#include "remediation/remediation_types.h"

#include <memory>
#include <string>
#include <vector>

namespace gno {
namespace remediation {

class IBackupStore {
public:
    virtual ~IBackupStore() = default;
    virtual SimpleResult save(const TransactionRecord& record) = 0;
    virtual Result<TransactionRecord> load(const std::string& transaction_id) = 0;
    virtual Result<std::vector<TransactionSummary>> list() = 0;
};

// File-backed store: one JSON file per transaction, atomic replace on save.
class JsonBackupStore : public IBackupStore {
public:
    explicit JsonBackupStore(std::string directory);

    SimpleResult save(const TransactionRecord& record) override;
    Result<TransactionRecord> load(const std::string& transaction_id) override;
    Result<std::vector<TransactionSummary>> list() override;

private:
    std::string pathFor(const std::string& transaction_id) const;

    std::string directory_;
};

} // namespace remediation
} // namespace gno
