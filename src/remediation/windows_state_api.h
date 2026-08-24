#pragma once

// Read/write access to the allowlisted parts of Windows state.
// Implemented for real in windows_state_api.cpp; tests provide fakes.

#include "remediation/remediation_types.h"

namespace gno {
namespace remediation {

class WindowsStateApi {
public:
    virtual ~WindowsStateApi() = default;

    virtual Result<DnsValue> getDns(const InterfaceTarget& iface);
    virtual SimpleResult setDns(const InterfaceTarget& iface, const DnsValue& value);
    virtual Result<MtuValue> getMtu(const InterfaceTarget& iface);
    virtual SimpleResult setMtu(const InterfaceTarget& iface, const MtuValue& value);
    virtual Result<RegistryData> getAllowedRegistry(AllowedRegistryKey key);
    virtual SimpleResult setAllowedRegistry(AllowedRegistryKey key, const RegistryData& value);
    virtual Result<PowerPlanValue> getPowerPlan();
    virtual SimpleResult setPowerPlan(const PowerPlanValue& value);
    virtual Result<FullscreenValue> getFullscreenOptimizations(const ExecutableTarget& exe);
    virtual SimpleResult setFullscreenOptimizations(const ExecutableTarget& exe, const FullscreenValue& value);
    virtual Result<PriorityLevel> getPriority(const ProcessTarget& process);
    virtual SimpleResult setPriority(const ProcessTarget& process, PriorityLevel level);
    // CS2 matchmaking ping filter (mm_dedicated_search_maxping in Steam cfg).
    virtual Result<Cs2MaxPingValue> getCs2MaxPing();
    virtual SimpleResult setCs2MaxPing(const Cs2MaxPingValue& value);

};

} // namespace remediation
} // namespace gno
