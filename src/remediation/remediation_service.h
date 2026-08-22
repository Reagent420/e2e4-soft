#pragma once

#include "remediation/backup_store.h"
#include "remediation/fix_transaction.h"
#include "remediation/windows_state_api.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace gno {
namespace remediation {

struct ActionView {
    ActionId id = ActionId::PowerPlan;
    std::string display_name;
    std::string description;
    ActionStatus status = ActionStatus::NotChecked;
    std::string status_text;    // localized status for UI tables
    std::string current_state;  // human-readable current value
    bool recommended = false;
};

struct ApplyOutcome {
    bool succeeded = false;
    RemediationError error = RemediationError::None;
    std::string detail;
    TransactionStatus status = TransactionStatus::Unprepared;
    std::vector<ActionOutcome> outcomes;
};

// High-level facade over the transaction engine: observe, apply, rollback.
// The target resolver may return nullopt for an action - it is then reported
// as Unsupported (observe) or skipped entirely (apply).
class RemediationService {
public:
    using TargetResolver =
        std::function<std::vector<std::optional<ActionTarget>>(const std::vector<FixAction*>&)>;

    RemediationService(WindowsStateApi& api, IBackupStore& backup_store,
                       TargetResolver resolver = {});

    Result<std::vector<ActionView>> observeAll() const;
    Result<ApplyOutcome> applyAll();
    Result<ApplyOutcome> rollback(const std::string& transaction_id);
    Result<std::vector<TransactionSummary>> history() const;

private:
    WindowsStateApi& api_;
    IBackupStore& backup_store_;
    TargetResolver resolver_;
};

std::string actionDisplayName(ActionId id);
std::string actionDescription(ActionId id);
std::string summarizeValue(const ActionValue& value);

} // namespace remediation
} // namespace gno
