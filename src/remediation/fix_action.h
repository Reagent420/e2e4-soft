#pragma once

#include "remediation/remediation_types.h"

namespace gno {
namespace remediation {

// One allowlisted remediation action: observe -> prepare -> apply / rollback.
class FixAction {
public:
    virtual ~FixAction() = default;

    virtual ActionId id() const = 0;
    virtual Result<ActionState> observe(const ActionTarget& target) = 0;
    virtual Result<PreparedAction> prepare(const ActionTarget& target, const ActionState& current) = 0;
    virtual Result<ActionState> apply(const PreparedAction& prepared) = 0;
    virtual Result<ActionState> rollback(const PreparedAction& prepared) = 0;
};

} // namespace remediation
} // namespace gno
