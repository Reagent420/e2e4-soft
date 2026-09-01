#pragma once

// Transactional applier for the declarative tweak catalogue.
// Same safety contract as the remediation engine: capture -> write -> verify,
// with a persisted JSON snapshot for rollback (atomic replace).

#include "core/tweak_registry.h"
#include "core/json_persistence.h"

#include <string>
#include <vector>

namespace gno {

struct TweakValue {
    bool existed = false;
    std::uint32_t dword = 0;
    std::string str;
    bool operator==(const TweakValue&) const = default;
};

struct TweakView {
    const TweakSpec* spec = nullptr;
    TweakValue current;
    bool differs = false;
    std::string current_text;
};

class ITweakAccess {
public:
    virtual ~ITweakAccess() = default;
    virtual TweakValue read(const TweakSpec& spec) = 0;
    virtual void write(const TweakSpec& spec, const TweakValue& v) = 0;
};

class TweakService {
public:
    TweakService(ITweakAccess& access, std::string rollback_dir,
                 std::string external_tweaks_dir = {})
        : access_(access), rollback_dir_(std::move(rollback_dir)) {}

    std::vector<TweakView> listViews(const std::string& category) const;

    // Applies every tweak of category (or all when empty). Persists rollback snapshot.
    std::string applyCategory(const std::string& category);

    // Rolls back the last applied batch using the persisted snapshot.
    std::string rollbackLast();

    bool hasRollbackSnapshot() const;

    // Returns IDs of tweaks that were just applied and need a reboot to take effect.
    std::vector<std::string> appliedNeedsReboot() const;

private:
    std::string snapshotPath() const;

    ITweakAccess& access_;
    std::string external_dir_;
    std::string rollback_dir_;
    std::vector<std::string> last_applied_needs_reboot_;
};

} // namespace gno
