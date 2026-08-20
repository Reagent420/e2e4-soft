#pragma once

#include "remediation/remediation_types.h"

#include <memory>

namespace gno {

enum class AllowedRegistryKey {
    TcpInitialRetransmissionTimeout,
    GameDvrEnabled,
    AppCaptureEnabled
};

class WindowsStateApi {
public:
    virtual ~WindowsStateApi() = default;

    virtual Result<DnsValue> getDns(const InterfaceId&) const = 0;
    virtual Result<std::monostate> setDns(
        const InterfaceId&, const DnsValue&) = 0;
    virtual Result<MtuValue> getMtu(const InterfaceId&) const = 0;
    virtual Result<std::monostate> setMtu(const InterfaceId&, MtuValue) = 0;
    virtual Result<RegistryValue> getAllowedRegistry(AllowedRegistryKey) const = 0;
    virtual Result<std::monostate> setAllowedRegistry(
        AllowedRegistryKey, const RegistryValue&) = 0;
    virtual Result<PowerPlanValue> getPowerPlan() const = 0;
    virtual Result<std::monostate> setPowerPlan(const PowerPlanValue&) = 0;
    virtual Result<FullscreenValue> getFullscreenOptimizations(
        const ExecutableIdentity&) const = 0;
    virtual Result<std::monostate> setFullscreenOptimizations(
        const ExecutableIdentity&, const FullscreenValue&) = 0;
    virtual Result<PriorityValue> getPriority(const ProcessIdentity&) const = 0;
    virtual Result<std::monostate> setPriority(
        const ProcessIdentity&, PriorityValue) = 0;
};

std::shared_ptr<WindowsStateApi> createWindowsStateApi();

} // namespace gno
