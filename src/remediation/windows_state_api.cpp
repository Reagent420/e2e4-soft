#include "remediation/windows_state_api.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#include <winreg.h>
#include <powrprof.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace gno {
namespace remediation {

namespace {

constexpr const char* kFallbackDns = "1.1.1.1";

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::string stripBraces(const std::string& guid) {
    const std::string lowered = lower(guid);
    const std::size_t start = lowered.find_first_of("0123456789abcdef");
    if (start == std::string::npos) return lowered;
    std::string out;
    for (std::size_t i = start; i < lowered.size() && out.size() < 36; ++i) {
        if (lowered[i] != '-') out += lowered[i];
        else out += '-';
        if (out.size() == 8 || out.size() == 13 || out.size() == 18 || out.size() == 23) out += '-';
    }
    // rebuild canonical 8-4-4-4-12 from hex only
    std::string hex;
    for (char c : lowered)
        if (std::isxdigit(static_cast<unsigned char>(c))) hex += c;
    if (hex.size() < 32) return lowered;
    return hex.substr(0, 8) + "-" + hex.substr(8, 4) + "-" + hex.substr(12, 4) + "-" +
           hex.substr(16, 4) + "-" + hex.substr(20, 12);
}

struct RegistryLocation {
    HKEY root;
    const char* subkey;
    const char* name;
};

RegistryLocation locationOf(AllowedRegistryKey key) {
    switch (key) {
        case AllowedRegistryKey::GameDvrEnabled:
            return {HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR",
                    "GameDVR_Enabled"};
        case AllowedRegistryKey::AppCaptureEnabled:
            return {HKEY_CURRENT_USER, "System\\GameConfigStore", "AppCaptureEnabled"};
        case AllowedRegistryKey::GameModeAllow:
            return {HKEY_CURRENT_USER, "Software\\Microsoft\\GameBar",
                    "AllowAutoGameMode"};
        case AllowedRegistryKey::TcpInitialRetransmissionTimeout:
            return {HKEY_LOCAL_MACHINE,
                    "SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters",
                    "TcpInitialRtt"};
    }
    return {HKEY_CURRENT_USER, "", ""};
}

Error mapLastError(const std::string& context) {
    const DWORD err = GetLastError();
    if (err == ERROR_ACCESS_DENIED)
        return Error::make(RemediationError::PermissionDenied, context + ": access denied");
    return Error::make(RemediationError::ApplyFailed,
                       context + ": error " + std::to_string(err));
}

// netsh expects the adapter FRIENDLY name, while the registry key uses the GUID.
static std::string friendlyNameForGuid(const std::string& guid) {
    ULONG size = 15000;
    std::vector<char> buffer(size);
    auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, nullptr, adapters, &size) ==
        ERROR_BUFFER_OVERFLOW) {
        buffer.resize(size);
        adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    }
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, nullptr, adapters, &size) != NO_ERROR)
        return guid;
    for (auto* a = adapters; a; a = a->Next) {
        char name_guid[128] = {};
        strncpy(name_guid, a->AdapterName, sizeof(name_guid) - 1);
        if (_stricmp(name_guid, guid.c_str()) == 0) {
            char friendly[256] = {};
            WideCharToMultiByte(CP_UTF8, 0, a->FriendlyName, -1,
                                friendly, sizeof(friendly), nullptr, nullptr);
            return friendly;
        }
    }
    return guid;
}
std::string runCommand(const std::string& command) {
    SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE read_end = nullptr;
    HANDLE write_end = nullptr;
    if (!CreatePipe(&read_end, &write_end, &sa, 0)) return {};

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = write_end;
    si.hStdError = write_end;

    PROCESS_INFORMATION pi{};
    std::string cmd_line = "cmd.exe /C " + command;
    if (!CreateProcessA(nullptr, cmd_line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &si, &pi)) {
        CloseHandle(read_end);
        CloseHandle(write_end);
        return {};
    }
    CloseHandle(write_end);

    std::string output;
    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(read_end, buffer, sizeof(buffer), &read, nullptr) && read > 0)
        output.append(buffer, read);
    CloseHandle(read_end);
    WaitForSingleObject(pi.hProcess, 15000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return output;
}

// Locates the CS2 user convars cfg via Steam registry path:
// <SteamPath>\userdata\<account>\730\local\cfg\cs2_user_convars_0_pc.cfg
static std::string findCs2CfgPath() {
    char steam_raw[512] = {};
    DWORD sz = sizeof(steam_raw);
    if (RegGetValueA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamPath",
                     RRF_RT_REG_SZ, nullptr, steam_raw, &sz) != ERROR_SUCCESS)
        return {};

    std::filesystem::path userdata = std::filesystem::path(steam_raw) / "userdata";
    std::error_code ec;
    if (!std::filesystem::exists(userdata, ec)) return {};

    for (const auto& account : std::filesystem::directory_iterator(userdata, ec)) {
        const auto cfg = account.path() / "730" / "local" / "cfg" /
                         "cs2_user_convars_0_pc.cfg";
        if (std::filesystem::exists(cfg, ec)) return cfg.string();
    }
    return {};
}

static std::string readCs2MaxPingLine(const std::string& path, bool& found_file) {
    found_file = !path.empty();
    if (path.empty()) return {};
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line))
        if (line.find("mm_dedicated_search_maxping") != std::string::npos)
            return line;
    return {};
}

} // namespace

Result<DnsValue> WindowsStateApi::getDns(const InterfaceTarget& iface) {
    HKEY key = nullptr;
    const std::string path =
        "SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces\\" + stripBraces(iface.id);
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
        return DnsValue{true, {}};

    char buffer[2048] = {};
    DWORD size = sizeof(buffer);
    const LSTATUS status = RegQueryValueExA(key, "NameServer", nullptr, nullptr,
                                            reinterpret_cast<LPBYTE>(buffer), &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS || buffer[0] == '\0')
        return DnsValue{true, {}};

    DnsValue value;
    value.automatic = false;
    std::istringstream stream(buffer);
    std::string server;
    while (std::getline(stream, server, ',')) {
        if (!server.empty()) value.servers.push_back(server);
    }
    return value;
}

SimpleResult WindowsStateApi::setDns(const InterfaceTarget& iface, const DnsValue& value) {
    const std::string guid = stripBraces(iface.id);
    std::ostringstream command;
    command << "netsh interface ip set dns name=\"" << friendlyNameForGuid(guid) << "\" ";
    if (value.automatic) {
        command << "source=dhcp";
    } else {
        command << "static addr=" << (value.servers.empty() ? kFallbackDns : value.servers[0]);
        command << " register=primary";
        if (value.servers.size() > 1)
            command << " >nul & netsh interface ip add dns name=\"" << friendlyNameForGuid(guid) << "\" addr="
                    << value.servers[1] << " index=2";
    }
    runCommand(command.str());

    auto verify = getDns(iface);
    if (!verify) return verify.error();
    if (!(verify.value() == value))
        return Fail(RemediationError::VerificationMismatch, "DNS did not change after netsh");
    return Ok();
}

Result<MtuValue> WindowsStateApi::getMtu(const InterfaceTarget& iface) {
    MIB_IFROW row{};
    row.dwIndex = static_cast<DWORD>(iface.index);
    if (GetIfEntry(&row) != NO_ERROR)
        return Fail(RemediationError::PreflightFailed, "interface not found");
    return MtuValue{row.dwMtu};
}

SimpleResult WindowsStateApi::setMtu(const InterfaceTarget& iface, const MtuValue& value) {
    std::ostringstream command;
    command << "netsh interface ipv4 set subinterface \"" << iface.index
            << "\" mtu=" << value.bytes << " store=persistent";
    runCommand(command.str());

    auto verify = getMtu(iface);
    if (!verify) return verify.error();
    if (verify.value().bytes != value.bytes)
        return Fail(RemediationError::VerificationMismatch, "MTU did not change after netsh");
    return Ok();
}

Result<RegistryData> WindowsStateApi::getAllowedRegistry(AllowedRegistryKey key) {
    const RegistryLocation loc = locationOf(key);
    HKEY handle = nullptr;
    RegistryData data;
    if (RegOpenKeyExA(loc.root, loc.subkey, 0, KEY_READ, &handle) != ERROR_SUCCESS)
        return data; // defaults: not existed

    DWORD value = 0;
    DWORD size = sizeof(value);
    DWORD type = 0;
    const LSTATUS status = RegQueryValueExA(handle, loc.name, nullptr, &type,
                                            reinterpret_cast<LPBYTE>(&value), &size);
    RegCloseKey(handle);
    data.key_existed = true;
    if (status == ERROR_SUCCESS && type == REG_DWORD) {
        data.existed = true;
        data.value = value;
    }
    return data;
}

SimpleResult WindowsStateApi::setAllowedRegistry(AllowedRegistryKey key, const RegistryData& value) {
    const RegistryLocation loc = locationOf(key);
    HKEY handle = nullptr;
    const REGSAM access = KEY_SET_VALUE | KEY_QUERY_VALUE;
    LSTATUS status = RegOpenKeyExA(loc.root, loc.subkey, 0, access, &handle);
    if (status != ERROR_SUCCESS) {
        if (!value.existed && !value.key_existed) return Ok();
        status = RegCreateKeyExA(loc.root, loc.subkey, 0, nullptr, 0, access, nullptr, &handle, nullptr);
        if (status != ERROR_SUCCESS) return mapLastError("registry open/create");
    }

    if (!value.existed) {
        RegDeleteValueA(handle, loc.name);
        RegCloseKey(handle);
        return Ok();
    }

    const DWORD raw = value.value;
    status = RegSetValueExA(handle, loc.name, 0, REG_DWORD,
                            reinterpret_cast<const BYTE*>(&raw), sizeof(raw));
    RegCloseKey(handle);
    if (status != ERROR_SUCCESS) return mapLastError("registry write");

    auto verify = getAllowedRegistry(key);
    if (!verify) return verify.error();
    if (!(verify.value() == value))
        return Fail(RemediationError::VerificationMismatch, "registry value did not persist");
    return Ok();
}

Result<PowerPlanValue> WindowsStateApi::getPowerPlan() {
    GUID* active = nullptr;
    if (PowerGetActiveScheme(nullptr, &active) != ERROR_SUCCESS || active == nullptr)
        return Fail(RemediationError::PreflightFailed, "cannot query active power scheme");

    char guid[64] = {};
    snprintf(guid, sizeof(guid),
             "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             static_cast<unsigned long>(active->Data1), active->Data2, active->Data3,
             active->Data4[0], active->Data4[1], active->Data4[2], active->Data4[3],
             active->Data4[4], active->Data4[5], active->Data4[6], active->Data4[7]);
    LocalFree(active);
    return PowerPlanValue{lower(guid)};
}

SimpleResult WindowsStateApi::setPowerPlan(const PowerPlanValue& value) {
    GUID scheme{};
    const std::string& g = value.identifier;
    if (g.size() != 36 ||
        sscanf(g.c_str(), "%8lx-%4hx-%4hx-%2hhx%2hhx-%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx",
               &scheme.Data1, &scheme.Data2, &scheme.Data3, &scheme.Data4[0], &scheme.Data4[1],
               &scheme.Data4[2], &scheme.Data4[3], &scheme.Data4[4], &scheme.Data4[5],
               &scheme.Data4[6], &scheme.Data4[7]) != 11)
        return Fail(RemediationError::InvalidTarget, "invalid power scheme guid");
    if (PowerSetActiveScheme(nullptr, &scheme) != ERROR_SUCCESS)
        return mapLastError("PowerSetActiveScheme");

    auto verify = getPowerPlan();
    if (!verify) return verify.error();
    if (verify.value().identifier != value.identifier)
        return Fail(RemediationError::VerificationMismatch, "power plan did not switch");
    return Ok();
}

Result<FullscreenValue> WindowsStateApi::getFullscreenOptimizations(const ExecutableTarget& exe) {
    HKEY key = nullptr;
    const std::string path = "Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers";
    FullscreenValue state;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, path.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
        return state;

    char buffer[1024] = {};
    DWORD size = sizeof(buffer);
    const LSTATUS status = RegQueryValueExA(key, exe.path.c_str(), nullptr, nullptr,
                                            reinterpret_cast<LPBYTE>(buffer), &size);
    RegCloseKey(key);
    state.key_existed = true;
    if (status == ERROR_SUCCESS) {
        state.existed = true;
        state.compatibility_flags = buffer;
    }
    return state;
}

SimpleResult WindowsStateApi::setFullscreenOptimizations(const ExecutableTarget& exe,
                                                         const FullscreenValue& value) {
    const std::string path = "Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers";
    HKEY handle = nullptr;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE,
                        nullptr, &handle, nullptr) != ERROR_SUCCESS)
        return mapLastError("compat layers key");

    SimpleResult result = Ok();
    if (!value.existed) {
        RegDeleteValueA(handle, exe.path.c_str());
    } else {
        const std::string flags = value.compatibility_flags;
        if (RegSetValueExA(handle, exe.path.c_str(), 0, REG_SZ,
                           reinterpret_cast<const BYTE*>(flags.c_str()),
                           static_cast<DWORD>(flags.size() + 1)) != ERROR_SUCCESS)
            result = mapLastError("compat layers write");
    }
    RegCloseKey(handle);

    if (result) {
        auto verify = getFullscreenOptimizations(exe);
        if (!verify) return verify.error();
        if (!(verify.value() == value))
            return Fail(RemediationError::VerificationMismatch, "fullscreen flags did not persist");
    }
    return result;
}

Result<MouseAccelValue> WindowsStateApi::getMouseAccel() {
    HKEY key = nullptr;
    MouseAccelValue v{1, 6, 10}; // Windows defaults = acceleration enabled
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Control Panel\\Mouse", 0, KEY_READ, &key)
        != ERROR_SUCCESS)
        return v;
    auto readStr = [&](const char* name) -> std::string {
        char buf[32] = {};
        DWORD sz = sizeof(buf);
        if (RegQueryValueExA(key, name, nullptr, nullptr, reinterpret_cast<LPBYTE>(buf), &sz)
            == ERROR_SUCCESS) return buf;
        return {};
    };
    const std::string sp = readStr("MouseSpeed");
    const std::string t1 = readStr("MouseThreshold1");
    const std::string t2 = readStr("MouseThreshold2");
    RegCloseKey(key);
    try {
        if (!sp.empty()) v.speed = static_cast<std::uint32_t>(std::stoul(sp));
        if (!t1.empty()) v.threshold1 = static_cast<std::uint32_t>(std::stoul(t1));
        if (!t2.empty()) v.threshold2 = static_cast<std::uint32_t>(std::stoul(t2));
    } catch (...) {}
    return v;
}

SimpleResult WindowsStateApi::setMouseAccel(const MouseAccelValue& value) {
    HKEY key = nullptr;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Control Panel\\Mouse", 0, KEY_SET_VALUE, &key)
        != ERROR_SUCCESS)
        return mapLastError("mouse key open");
    auto writeStr = [&](const char* name, std::uint32_t num) -> bool {
        const std::string sval = std::to_string(num);
        return RegSetValueExA(key, name, 0, REG_SZ,
                              reinterpret_cast<const BYTE*>(sval.c_str()),
                              static_cast<DWORD>(sval.size() + 1)) == ERROR_SUCCESS;
    };
    bool ok = writeStr("MouseSpeed", value.speed);
    ok = writeStr("MouseThreshold1", value.threshold1) && ok;
    ok = writeStr("MouseThreshold2", value.threshold2) && ok;
    RegCloseKey(key);
    if (!ok) return mapLastError("mouse write");
    auto verify = getMouseAccel();
    if (!verify) return verify.error();
    if (!(verify.value() == value))
        return Fail(RemediationError::VerificationMismatch,
                    "mouse accel did not persist (relogin may be required)");
    return Ok();
}

Result<Cs2MaxPingValue> WindowsStateApi::getCs2MaxPing() {
    bool have = false;
    const std::string line = readCs2MaxPingLine(findCs2CfgPath(), have);
    Cs2MaxPingValue v{};
    if (!have)
        return Fail(RemediationError::Unsupported,
                    "CS2 \xd0\xbd\xd0\xb5 \xd0\xbd\xd0\xb0\xd0\xb9\xd0\xb4\xd0\xb5\xd0\xbd (Steam cfg)");
    if (line.empty()) return v; // default 60 when convar missing

    const auto q1 = line.find('"');
    const auto q2 = line.find('"', q1 + 1);
    const auto q3 = line.find('"', q2 + 1);
    const auto q4 = line.find('"', q3 + 1);
    if (q1 != std::string::npos && q4 != std::string::npos && q4 > q3 && q3 > q2 && q2 > q1) {
        try { v.max_ping = static_cast<std::uint32_t>(std::stoul(line.substr(q3 + 1, q4 - q3 - 1))); }
        catch (...) { v.max_ping = 60; }
    }
    return v;
}

SimpleResult WindowsStateApi::setCs2MaxPing(const Cs2MaxPingValue& value) {
    const std::string path = findCs2CfgPath();
    if (path.empty())
        return Fail(RemediationError::Unsupported,
                    "CS2 \xd0\xbd\xd0\xb5 \xd0\xbd\xd0\xb0\xd0\xb9\xd0\xb4\xd0\xb5\xd0\xbd (Steam cfg)");

    std::ifstream in(path);
    std::ostringstream buf;
    buf << in.rdbuf();
    in.close();
    std::string content = buf.str();

    const std::string needle = "mm_dedicated_search_maxping";
    const auto pos = content.find(needle);
    const std::string newline =
        "\"" + needle + "\"=\"" + std::to_string(value.max_ping) + "\"";

    if (pos == std::string::npos)
        content += "\n" + newline + "\n";
    else {
        const auto line_end = content.find('\n', pos);
        content.replace(pos, (line_end == std::string::npos ? content.size() : line_end) - pos,
                        newline);
    }

    std::ofstream out(path, std::ios::trunc);
    out << content;
    out.flush();
    if (!out) return mapLastError("CS2 cfg write");

    auto verify = getCs2MaxPing();
    if (!verify) return verify.error();
    if (verify.value().max_ping != value.max_ping)
        return Fail(RemediationError::VerificationMismatch, "cs2 maxping did not persist");
    return Ok();
}

Result<PriorityLevel> WindowsStateApi::getPriority(const ProcessTarget& process) {
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process.pid);
    if (!handle) return mapLastError("OpenProcess");
    const DWORD cls = GetPriorityClass(handle);
    CloseHandle(handle);
    switch (cls) {
        case ABOVE_NORMAL_PRIORITY_CLASS: return PriorityLevel::AboveNormal;
        case HIGH_PRIORITY_CLASS: return PriorityLevel::High;
        default: return PriorityLevel::Normal;
    }
}

SimpleResult WindowsStateApi::setPriority(const ProcessTarget& process, PriorityLevel level) {
    HANDLE handle = OpenProcess(PROCESS_SET_INFORMATION, FALSE, process.pid);
    if (!handle) return mapLastError("OpenProcess");
    const DWORD cls = level == PriorityLevel::AboveNormal ? ABOVE_NORMAL_PRIORITY_CLASS
                      : level == PriorityLevel::High ? HIGH_PRIORITY_CLASS
                                                     : NORMAL_PRIORITY_CLASS;
    const BOOL ok = SetPriorityClass(handle, cls);
    CloseHandle(handle);
    if (!ok) return mapLastError("SetPriorityClass");

    auto verify = getPriority(process);
    if (!verify) return verify.error();
    if (verify.value() != level)
        return Fail(RemediationError::VerificationMismatch, "priority class did not apply");
    return Ok();
}

} // namespace remediation
} // namespace gno
