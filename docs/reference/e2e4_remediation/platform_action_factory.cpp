#include "remediation/platform_action_factory.h"

#include "remediation/windows/windows_fix_action.h"

#include <utility>

namespace gno {
namespace {

template <typename T>
Result<T> unsupported() {
    return {T{}, RemediationError::Unsupported,
            "Windows remediation is unavailable on this platform"};
}

class UnsupportedWindowsStateApi final : public WindowsStateApi {
public:
    Result<DnsValue> getDns(const InterfaceId&) const override { return unsupported<DnsValue>(); }
    Result<std::monostate> setDns(const InterfaceId&, const DnsValue&) override { return unsupported<std::monostate>(); }
    Result<MtuValue> getMtu(const InterfaceId&) const override { return unsupported<MtuValue>(); }
    Result<std::monostate> setMtu(const InterfaceId&, MtuValue) override { return unsupported<std::monostate>(); }
    Result<RegistryValue> getAllowedRegistry(AllowedRegistryKey) const override { return unsupported<RegistryValue>(); }
    Result<std::monostate> setAllowedRegistry(AllowedRegistryKey, const RegistryValue&) override { return unsupported<std::monostate>(); }
    Result<PowerPlanValue> getPowerPlan() const override { return unsupported<PowerPlanValue>(); }
    Result<std::monostate> setPowerPlan(const PowerPlanValue&) override { return unsupported<std::monostate>(); }
    Result<FullscreenValue> getFullscreenOptimizations(const ExecutableIdentity&) const override { return unsupported<FullscreenValue>(); }
    Result<std::monostate> setFullscreenOptimizations(const ExecutableIdentity&, const FullscreenValue&) override { return unsupported<std::monostate>(); }
    Result<PriorityValue> getPriority(const ProcessIdentity&) const override { return unsupported<PriorityValue>(); }
    Result<std::monostate> setPriority(const ProcessIdentity&, PriorityValue) override { return unsupported<std::monostate>(); }
};

} // namespace

std::vector<std::unique_ptr<FixAction>> createPlatformFixActions(
    std::shared_ptr<WindowsStateApi> api) {
#ifdef _WIN32
    if (!api) api = createWindowsStateApi();
#else
    (void)api;
    api = std::make_shared<UnsupportedWindowsStateApi>();
#endif
    return createWindowsFixActions(std::move(api));
}

} // namespace gno
