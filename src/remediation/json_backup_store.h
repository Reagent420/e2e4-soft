#pragma once

#include "remediation/backup_store.h"

#include <filesystem>

namespace gno {

class JsonBackupStore final : public BackupStore {
public:
    explicit JsonBackupStore(std::filesystem::path storage_root = {});

    Result<std::monostate> save(const TransactionRecord& record) override;
    Result<TransactionRecord> load(std::string_view transaction_id) const override;
    Result<std::vector<TransactionSummary>> list() const override;

private:
    std::filesystem::path storage_root_;
};

} // namespace gno
