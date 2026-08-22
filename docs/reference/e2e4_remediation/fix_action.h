#pragma once

#include "remediation/remediation_types.h"

namespace gno {

class FixAction {
public:
    virtual ~FixAction() = default;
    virtual ActionId id() const noexcept = 0;
    virtual Result<ActionState> observe(
        const ActionTarget&, const CancellationToken&) const = 0;
    virtual Result<PreparedAction> prepare(
        const ActionTarget&, const ActionState&) const = 0;
    virtual Result<ActionState> apply(
        const PreparedAction&, const CancellationToken&) = 0;
    virtual Result<ActionState> rollback(
        const PreparedAction&, const CancellationToken&) = 0;
};

} // namespace gno
