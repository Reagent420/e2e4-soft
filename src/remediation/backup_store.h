#pragma once

#include "remediation/remediation_types.h"

#include <string_view>

namespace gno {

class BackupStore {
public:
    virtual ~BackupStore() = default;
    virtual Result<std::monostate> save(const TransactionRecord&) = 0;
    virtual Result<TransactionRecord> load(std::string_view transaction_id) const = 0;
    virtual Result<std::vector<TransactionSummary>> list() const = 0;
};

} // namespace gno
