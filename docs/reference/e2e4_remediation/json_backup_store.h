#pragma once

#include "remediation/backup_store.h"

#include <filesystem>
#include <functional>
#include <system_error>

namespace gno {

class JsonBackupStore final : public BackupStore {
public:
    enum class FailurePoint {
        Write,
        FlushFile,
        Replace,
        Remove,
        SyncDirectory,
        BeforeReadVerification
    };
    using FailureInjector = std::function<bool(
        FailurePoint, const std::filesystem::path&)>;

    explicit JsonBackupStore(
        std::filesystem::path storage_root = {},
        FailureInjector failure_injector = {});

    Result<std::monostate> save(const TransactionRecord& record) override;
    Result<TransactionRecord> load(std::string_view transaction_id) const override;
    Result<std::vector<TransactionSummary>> list() const override;

private:
    std::filesystem::path storage_root_;
    FailureInjector failure_injector_;
};

} // namespace gno
