#pragma once

#include "remediation/backup_store.h"

#include <filesystem>
#include <functional>
#include <system_error>

namespace gno {

class JsonBackupStore final : public BackupStore {
public:
    using FileRemover = std::function<bool(
        const std::filesystem::path&, std::error_code&)>;

    explicit JsonBackupStore(
        std::filesystem::path storage_root = {}, FileRemover file_remover = {});

    Result<std::monostate> save(const TransactionRecord& record) override;
    Result<TransactionRecord> load(std::string_view transaction_id) const override;
    Result<std::vector<TransactionSummary>> list() const override;

private:
    std::filesystem::path storage_root_;
    FileRemover file_remover_;
};

} // namespace gno
