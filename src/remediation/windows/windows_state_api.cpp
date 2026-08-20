#include "remediation/windows/windows_state_api.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <objbase.h>
#include <powrprof.h>
#endif

namespace gno {
namespace {

template <typename T>
Result<T> success(T value) {
    return {std::move(value), RemediationError::None, {}};
}

template <typename T>
Result<T> failure(RemediationError error, std::string detail) {
    return {T{}, error, std::move(detail)};
}

#ifdef _WIN32

RemediationError mapError(DWORD error) noexcept {
    switch (error) {
    case ERROR_ACCESS_DENIED:
    case ERROR_PRIVILEGE_NOT_HELD:
        return RemediationError::PermissionDenied;
    case ERROR_NOT_SUPPORTED:
    case ERROR_PROC_NOT_FOUND:
    case ERROR_CALL_NOT_IMPLEMENTED:
        return RemediationError::Unsupported;
    case ERROR_FILE_NOT_FOUND:
    case ERROR_NOT_FOUND:
    case ERROR_INVALID_PARAMETER:
        return RemediationError::InvalidTarget;
    default:
        return RemediationError::ApplyFailed;
    }
}

template <typename T>
Result<T> winFailure(DWORD error, const char* operation) {
    return failure<T>(mapError(error),
                      std::string(operation) + " failed with Win32 error " +
                          std::to_string(error));
}

std::wstring toWide(std::string_view text) {
    if (text.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                          static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                            static_cast<int>(text.size()), result.data(), count) != count) {
        return {};
    }
    return result;
}

std::string toUtf8(std::wstring_view text) {
    if (text.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                                          static_cast<int>(text.size()), nullptr, 0,
                                          nullptr, nullptr);
    if (count <= 0) return {};
    std::string result(static_cast<std::size_t>(count), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                            static_cast<int>(text.size()), result.data(), count,
                            nullptr, nullptr) != count) {
        return {};
    }
    return result;
}

Result<GUID> parseGuid(std::string_view text) {
    const auto wide = toWide(text);
    GUID value{};
    if (wide.empty() || FAILED(CLSIDFromString(wide.c_str(), &value))) {
        return failure<GUID>(RemediationError::InvalidTarget, "invalid GUID");
    }
    return success(value);
}

std::string formatGuid(const GUID& value) {
    wchar_t buffer[39]{};
    if (StringFromGUID2(value, buffer, static_cast<int>(std::size(buffer))) == 0) return {};
    std::string result = toUtf8(buffer);
    if (result.size() == 38 && result.front() == '{' && result.back() == '}') {
        result = result.substr(1, 36);
    }
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

struct RegistryLocation {
    HKEY root;
    const wchar_t* path;
    const wchar_t* name;
    bool unsigned_value;
};

RegistryLocation registryLocation(AllowedRegistryKey key) {
    static constexpr wchar_t tcp_path[] =
        L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters";
    switch (key) {
    case AllowedRegistryKey::TcpInitialRetransmissionTimeout:
        return {HKEY_LOCAL_MACHINE, tcp_path, L"InitialRttData", false};
    case AllowedRegistryKey::GameDvrEnabled:
        return {HKEY_CURRENT_USER,
                L"System\\GameConfigStore", L"GameDVR_Enabled", true};
    case AllowedRegistryKey::AppCaptureEnabled:
        return {HKEY_CURRENT_USER,
                L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR",
                L"AppCaptureEnabled", true};
    }
    return {nullptr, nullptr, nullptr, false};
}

class RegistryKey {
public:
    RegistryKey() = default;
    ~RegistryKey() { if (key_) RegCloseKey(key_); }
    RegistryKey(const RegistryKey&) = delete;
    RegistryKey& operator=(const RegistryKey&) = delete;
    HKEY* out() noexcept { return &key_; }
    HKEY get() const noexcept { return key_; }
private:
    HKEY key_ = nullptr;
};

Result<RegistryValue> readRegistry(AllowedRegistryKey key) {
    const auto location = registryLocation(key);
    if (!location.root) return failure<RegistryValue>(RemediationError::InvalidTarget, "registry key is not allowlisted");
    RegistryKey opened;
    const LONG opened_result = RegOpenKeyExW(location.root, location.path, 0,
                                              KEY_QUERY_VALUE, opened.out());
    if (opened_result == ERROR_FILE_NOT_FOUND) {
        return success(RegistryValue{false, {}, false});
    }
    if (opened_result != ERROR_SUCCESS) return winFailure<RegistryValue>(opened_result, "RegOpenKeyExW");

    DWORD type = 0;
    DWORD value = 0;
    DWORD size = sizeof(value);
    const LONG result = RegQueryValueExW(opened.get(), location.name, nullptr, &type,
                                         reinterpret_cast<BYTE*>(&value), &size);
    if (result == ERROR_FILE_NOT_FOUND) {
        return success(RegistryValue{false, {}, true});
    }
    if (result != ERROR_SUCCESS) return winFailure<RegistryValue>(result, "RegQueryValueExW");
    if (type != REG_DWORD || size != sizeof(value)) {
        return failure<RegistryValue>(RemediationError::PreflightFailed,
                                      "allowlisted registry value is not REG_DWORD");
    }
    RegistryValue registry;
    registry.existed = true;
    registry.value = location.unsigned_value ? RegistryScalar{value}
                                             : RegistryScalar{int64_t{value}};
    return success(std::move(registry));
}

Result<std::monostate> writeRegistry(
    AllowedRegistryKey key, const RegistryValue& value) {
    const auto location = registryLocation(key);
    if (!location.root) return failure<std::monostate>(RemediationError::InvalidTarget, "registry key is not allowlisted");
    RegistryKey opened;
    DWORD disposition = 0;
    const LONG opened_result = RegCreateKeyExW(
        location.root, location.path, 0, nullptr, REG_OPTION_NON_VOLATILE,
        KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, opened.out(), &disposition);
    (void)disposition;
    if (opened_result != ERROR_SUCCESS) return winFailure<std::monostate>(opened_result, "RegCreateKeyExW");
    if (!value.existed) {
        const LONG removed = RegDeleteValueW(opened.get(), location.name);
        if (removed != ERROR_SUCCESS && removed != ERROR_FILE_NOT_FOUND) {
            return winFailure<std::monostate>(removed, "RegDeleteValueW");
        }
        if (!value.key_existed) {
            const LONG removed_key = RegDeleteKeyW(location.root, location.path);
            if (removed_key != ERROR_SUCCESS && removed_key != ERROR_FILE_NOT_FOUND) {
                return winFailure<std::monostate>(removed_key, "RegDeleteKeyW");
            }
        }
        return success(std::monostate{});
    }

    uint32_t scalar = 0;
    if (const auto* unsigned_value = std::get_if<uint32_t>(&value.value)) {
        scalar = *unsigned_value;
    } else if (const auto* signed_value = std::get_if<int64_t>(&value.value);
               signed_value && *signed_value >= 0 &&
               *signed_value <= std::numeric_limits<uint32_t>::max()) {
        scalar = static_cast<uint32_t>(*signed_value);
    } else {
        return failure<std::monostate>(RemediationError::InvalidTarget,
                                       "registry value must be a DWORD");
    }
    const LONG written = RegSetValueExW(
        opened.get(), location.name, 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&scalar), sizeof(scalar));
    if (written != ERROR_SUCCESS) return winFailure<std::monostate>(written, "RegSetValueExW");
    return success(std::monostate{});
}

Result<std::monostate> validateProcess(
    const ProcessIdentity& expected, HANDLE process) {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) {
        return winFailure<std::monostate>(GetLastError(), "GetProcessTimes");
    }
    const uint64_t creation = (static_cast<uint64_t>(created.dwHighDateTime) << 32U) |
                              created.dwLowDateTime;
    if (creation != expected.creation_time) {
        return failure<std::monostate>(RemediationError::InvalidTarget,
                                       "process creation identity changed");
    }
    std::wstring path(32768, L'\0');
    DWORD size = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process, 0, path.data(), &size)) {
        return winFailure<std::monostate>(GetLastError(), "QueryFullProcessImageNameW");
    }
    path.resize(size);
    const auto expected_path = toWide(expected.executable_path);
    if (expected_path.empty() || _wcsicmp(path.c_str(), expected_path.c_str()) != 0) {
        return failure<std::monostate>(RemediationError::InvalidTarget,
                                       "process executable identity changed");
    }
    return success(std::monostate{});
}

Result<std::wstring> validateExecutable(const ExecutableIdentity& executable) {
    const auto requested = toWide(executable.canonical_path);
    if (requested.empty()) {
        return failure<std::wstring>(RemediationError::InvalidTarget,
                                     "invalid executable path");
    }
    std::wstring canonical(32768, L'\0');
    const DWORD length = GetFullPathNameW(
        requested.c_str(), static_cast<DWORD>(canonical.size()), canonical.data(), nullptr);
    if (length == 0 || length >= canonical.size()) {
        return winFailure<std::wstring>(GetLastError(), "GetFullPathNameW");
    }
    canonical.resize(length);
    const DWORD attributes = GetFileAttributesW(canonical.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
        _wcsicmp(canonical.c_str(), requested.c_str()) != 0) {
        return failure<std::wstring>(RemediationError::InvalidTarget,
                                     "selected executable identity is stale");
    }
    return success(std::move(canonical));
}

Result<NET_LUID> validateInterface(const InterfaceId& interface_id) {
    const auto guid = parseGuid(interface_id.value);
    if (!guid.ok() || interface_id.luid == 0) {
        return failure<NET_LUID>(RemediationError::InvalidTarget,
                                 "invalid interface identity");
    }
    NET_LUID luid{};
    const DWORD result = ConvertInterfaceGuidToLuid(&guid.value, &luid);
    if (result != NO_ERROR) return winFailure<NET_LUID>(result, "ConvertInterfaceGuidToLuid");
    if (luid.Value != interface_id.luid) {
        return failure<NET_LUID>(RemediationError::InvalidTarget,
                                 "interface GUID/LUID identity changed");
    }
    return success(luid);
}

class ProcessHandle {
public:
    explicit ProcessHandle(HANDLE handle) : handle_(handle) {}
    ~ProcessHandle() { if (handle_) CloseHandle(handle_); }
    HANDLE get() const noexcept { return handle_; }
private:
    HANDLE handle_ = nullptr;
};

DWORD nativePriority(PriorityValue value) {
    switch (value) {
    case PriorityValue::Idle: return IDLE_PRIORITY_CLASS;
    case PriorityValue::BelowNormal: return BELOW_NORMAL_PRIORITY_CLASS;
    case PriorityValue::Normal: return NORMAL_PRIORITY_CLASS;
    case PriorityValue::AboveNormal: return ABOVE_NORMAL_PRIORITY_CLASS;
    case PriorityValue::High: return HIGH_PRIORITY_CLASS;
    case PriorityValue::Realtime: return REALTIME_PRIORITY_CLASS;
    }
    return 0;
}

Result<PriorityValue> typedPriority(DWORD value) {
    switch (value) {
    case IDLE_PRIORITY_CLASS: return success(PriorityValue::Idle);
    case BELOW_NORMAL_PRIORITY_CLASS: return success(PriorityValue::BelowNormal);
    case NORMAL_PRIORITY_CLASS: return success(PriorityValue::Normal);
    case ABOVE_NORMAL_PRIORITY_CLASS: return success(PriorityValue::AboveNormal);
    case HIGH_PRIORITY_CLASS: return success(PriorityValue::High);
    case REALTIME_PRIORITY_CLASS: return success(PriorityValue::Realtime);
    default:
        return failure<PriorityValue>(RemediationError::InternalFailure,
                                      "unknown Windows priority class");
    }
}

class NativeWindowsStateApi final : public WindowsStateApi {
public:
    Result<DnsValue> getDns(const InterfaceId& interface_id) const override {
#if defined(DNS_INTERFACE_SETTINGS_VERSION1)
        const auto guid = parseGuid(interface_id.value);
        if (!guid.ok()) return failure<DnsValue>(guid.error, guid.detail);
        const auto luid = validateInterface(interface_id);
        if (!luid.ok()) return failure<DnsValue>(luid.error, luid.detail);
        using GetFn = DWORD(WINAPI*)(GUID, DNS_INTERFACE_SETTINGS*);
        using FreeFn = VOID(WINAPI*)(DNS_INTERFACE_SETTINGS*);
        const HMODULE module = GetModuleHandleW(L"iphlpapi.dll");
        const auto get = module ? reinterpret_cast<GetFn>(GetProcAddress(module, "GetInterfaceDnsSettings")) : nullptr;
        const auto release = module ? reinterpret_cast<FreeFn>(GetProcAddress(module, "FreeInterfaceDnsSettings")) : nullptr;
        if (!get || !release) return failure<DnsValue>(RemediationError::Unsupported, "per-interface DNS API is unavailable");
        DNS_INTERFACE_SETTINGS settings{};
        settings.Version = DNS_INTERFACE_SETTINGS_VERSION1;
        const DWORD result = get(guid.value, &settings);
        if (result != NO_ERROR) return winFailure<DnsValue>(result, "GetInterfaceDnsSettings");
        DnsValue value;
        value.automatic = settings.NameServer == nullptr || settings.NameServer[0] == L'\0';
        if (!value.automatic) {
            std::wstring servers = settings.NameServer;
            std::replace(servers.begin(), servers.end(), L',', L' ');
            std::wistringstream input(servers);
            std::wstring server;
            while (input >> server) {
                const auto parsed = Ipv4Address::parse(toUtf8(server));
                if (!parsed || parsed->isUnspecified() || value.servers.size() >= kMaxDnsServers) {
                    release(&settings);
                    return failure<DnsValue>(RemediationError::PreflightFailed,
                                             "DNS API returned an invalid IPv4 server list");
                }
                value.servers.push_back(*parsed);
            }
        }
        release(&settings);
        return success(std::move(value));
#else
        (void)interface_id;
        return failure<DnsValue>(RemediationError::Unsupported, "Windows SDK lacks per-interface DNS API");
#endif
    }

    Result<std::monostate> setDns(
        const InterfaceId& interface_id, const DnsValue& value) override {
#if defined(DNS_INTERFACE_SETTINGS_VERSION1)
        const auto guid = parseGuid(interface_id.value);
        if (!guid.ok()) return failure<std::monostate>(guid.error, guid.detail);
        const auto luid = validateInterface(interface_id);
        if (!luid.ok()) return failure<std::monostate>(luid.error, luid.detail);
        using SetFn = DWORD(WINAPI*)(GUID, const DNS_INTERFACE_SETTINGS*);
        const HMODULE module = GetModuleHandleW(L"iphlpapi.dll");
        const auto set = module ? reinterpret_cast<SetFn>(GetProcAddress(module, "SetInterfaceDnsSettings")) : nullptr;
        if (!set) return failure<std::monostate>(RemediationError::Unsupported, "per-interface DNS API is unavailable");
        std::wstring nameservers;
        for (const auto& server : value.servers) {
            if (!nameservers.empty()) nameservers += L",";
            nameservers += toWide(server.toString());
        }
        DNS_INTERFACE_SETTINGS settings{};
        settings.Version = DNS_INTERFACE_SETTINGS_VERSION1;
        settings.Flags = DNS_SETTING_NAMESERVER;
        settings.NameServer = value.automatic ? nullptr : nameservers.data();
        const DWORD result = set(guid.value, &settings);
        if (result != NO_ERROR) return winFailure<std::monostate>(result, "SetInterfaceDnsSettings");
        return success(std::monostate{});
#else
        (void)interface_id;
        (void)value;
        return failure<std::monostate>(RemediationError::Unsupported, "Windows SDK lacks per-interface DNS API");
#endif
    }

    Result<MtuValue> getMtu(const InterfaceId& interface_id) const override {
        const auto luid = validateInterface(interface_id);
        if (!luid.ok()) return failure<MtuValue>(luid.error, luid.detail);
        MIB_IPINTERFACE_ROW row{};
        InitializeIpInterfaceEntry(&row);
        row.Family = AF_INET;
        row.InterfaceLuid = luid.value;
        DWORD result = GetIpInterfaceEntry(&row);
        if (result != NO_ERROR) return winFailure<MtuValue>(result, "GetIpInterfaceEntry");
        return success(MtuValue{row.NlMtu});
    }

    Result<std::monostate> setMtu(
        const InterfaceId& interface_id, MtuValue value) override {
        const auto luid = validateInterface(interface_id);
        if (!luid.ok()) return failure<std::monostate>(luid.error, luid.detail);
        MIB_IPINTERFACE_ROW row{};
        InitializeIpInterfaceEntry(&row);
        row.Family = AF_INET;
        row.InterfaceLuid = luid.value;
        DWORD result = GetIpInterfaceEntry(&row);
        if (result != NO_ERROR) return winFailure<std::monostate>(result, "GetIpInterfaceEntry");
        row.SitePrefixLength = 0;
        row.NlMtu = value.bytes;
        result = SetIpInterfaceEntry(&row);
        if (result != NO_ERROR) return winFailure<std::monostate>(result, "SetIpInterfaceEntry");
        return success(std::monostate{});
    }

    Result<RegistryValue> getAllowedRegistry(AllowedRegistryKey key) const override {
        return readRegistry(key);
    }
    Result<std::monostate> setAllowedRegistry(
        AllowedRegistryKey key, const RegistryValue& value) override {
        return writeRegistry(key, value);
    }

    Result<PowerPlanValue> getPowerPlan() const override {
        GUID* active = nullptr;
        const DWORD result = PowerGetActiveScheme(nullptr, &active);
        if (result != ERROR_SUCCESS) return winFailure<PowerPlanValue>(result, "PowerGetActiveScheme");
        std::unique_ptr<GUID, decltype(&LocalFree)> holder(active, &LocalFree);
        const auto identifier = formatGuid(*active);
        if (identifier.empty()) return failure<PowerPlanValue>(RemediationError::InternalFailure, "invalid active power plan GUID");
        return success(PowerPlanValue{identifier});
    }

    Result<std::monostate> setPowerPlan(const PowerPlanValue& value) override {
        const auto guid = parseGuid(value.identifier);
        if (!guid.ok()) return failure<std::monostate>(guid.error, guid.detail);
        const DWORD result = PowerSetActiveScheme(nullptr, &guid.value);
        if (result != ERROR_SUCCESS) return winFailure<std::monostate>(result, "PowerSetActiveScheme");
        return success(std::monostate{});
    }

    Result<FullscreenValue> getFullscreenOptimizations(
        const ExecutableIdentity& executable) const override {
        const auto path = validateExecutable(executable);
        if (!path.ok()) return failure<FullscreenValue>(path.error, path.detail);
        RegistryKey opened;
        const LONG opened_result = RegOpenKeyExW(
            HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers",
            0, KEY_QUERY_VALUE, opened.out());
        if (opened_result == ERROR_FILE_NOT_FOUND) {
            return success(FullscreenValue{false, {}, false});
        }
        if (opened_result != ERROR_SUCCESS) return winFailure<FullscreenValue>(opened_result, "RegOpenKeyExW");
        DWORD type = 0;
        DWORD size = 0;
        LONG result = RegQueryValueExW(opened.get(), path.value.c_str(), nullptr, &type, nullptr, &size);
        if (result == ERROR_FILE_NOT_FOUND) {
            return success(FullscreenValue{false, {}, true});
        }
        if (result != ERROR_SUCCESS) return winFailure<FullscreenValue>(result, "RegQueryValueExW");
        if (type != REG_SZ || size > (kMaxRegistryStringLength + 1) * sizeof(wchar_t)) {
            return failure<FullscreenValue>(RemediationError::PreflightFailed, "fullscreen compatibility value is invalid");
        }
        std::wstring text(size / sizeof(wchar_t), L'\0');
        result = RegQueryValueExW(opened.get(), path.value.c_str(), nullptr, &type,
                                  reinterpret_cast<BYTE*>(text.data()), &size);
        if (result != ERROR_SUCCESS) return winFailure<FullscreenValue>(result, "RegQueryValueExW");
        if (!text.empty() && text.back() == L'\0') text.pop_back();
        return success(FullscreenValue{true, toUtf8(text), true});
    }

    Result<std::monostate> setFullscreenOptimizations(
        const ExecutableIdentity& executable, const FullscreenValue& value) override {
        const auto path = validateExecutable(executable);
        if (!path.ok()) return failure<std::monostate>(path.error, path.detail);
        RegistryKey opened;
        DWORD disposition = 0;
        const LONG opened_result = RegCreateKeyExW(
            HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers",
            0, nullptr, REG_OPTION_NON_VOLATILE, KEY_QUERY_VALUE | KEY_SET_VALUE,
            nullptr, opened.out(), &disposition);
        (void)disposition;
        if (opened_result != ERROR_SUCCESS) return winFailure<std::monostate>(opened_result, "RegCreateKeyExW");
        if (!value.existed) {
            const LONG removed = RegDeleteValueW(opened.get(), path.value.c_str());
            if (removed != ERROR_SUCCESS && removed != ERROR_FILE_NOT_FOUND) {
                return winFailure<std::monostate>(removed, "RegDeleteValueW");
            }
            if (!value.key_existed) {
                const LONG removed_key = RegDeleteKeyW(
                    HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers");
                if (removed_key != ERROR_SUCCESS && removed_key != ERROR_FILE_NOT_FOUND) {
                    return winFailure<std::monostate>(removed_key, "RegDeleteKeyW");
                }
            }
            return success(std::monostate{});
        }
        const auto flags = toWide(value.compatibility_flags);
        if (flags.empty() || flags.size() > kMaxRegistryStringLength) {
            return failure<std::monostate>(RemediationError::InvalidTarget, "invalid fullscreen compatibility flags");
        }
        const DWORD bytes = static_cast<DWORD>((flags.size() + 1) * sizeof(wchar_t));
        const LONG written = RegSetValueExW(opened.get(), path.value.c_str(), 0, REG_SZ,
                                            reinterpret_cast<const BYTE*>(flags.c_str()), bytes);
        if (written != ERROR_SUCCESS) return winFailure<std::monostate>(written, "RegSetValueExW");
        return success(std::monostate{});
    }

    Result<PriorityValue> getPriority(const ProcessIdentity& identity) const override {
        ProcessHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, identity.pid));
        if (!process.get()) return winFailure<PriorityValue>(GetLastError(), "OpenProcess");
        const auto valid = validateProcess(identity, process.get());
        if (!valid.ok()) return failure<PriorityValue>(valid.error, valid.detail);
        const DWORD value = GetPriorityClass(process.get());
        if (value == 0) return winFailure<PriorityValue>(GetLastError(), "GetPriorityClass");
        return typedPriority(value);
    }

    Result<std::monostate> setPriority(
        const ProcessIdentity& identity, PriorityValue value) override {
        if (value == PriorityValue::Realtime) {
            return failure<std::monostate>(RemediationError::InvalidTarget, "realtime priority is forbidden");
        }
        ProcessHandle process(OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_INFORMATION,
            FALSE, identity.pid));
        if (!process.get()) return winFailure<std::monostate>(GetLastError(), "OpenProcess");
        const auto valid = validateProcess(identity, process.get());
        if (!valid.ok()) return valid;
        if (!SetPriorityClass(process.get(), nativePriority(value))) {
            return winFailure<std::monostate>(GetLastError(), "SetPriorityClass");
        }
        return success(std::monostate{});
    }
};

#else

class NativeWindowsStateApi final : public WindowsStateApi {
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

private:
    template <typename T>
    static Result<T> unsupported() {
        return failure<T>(RemediationError::Unsupported,
                          "Windows state API is unavailable on this platform");
    }
};

#endif

} // namespace

std::shared_ptr<WindowsStateApi> createWindowsStateApi() {
    return std::make_shared<NativeWindowsStateApi>();
}

} // namespace gno
