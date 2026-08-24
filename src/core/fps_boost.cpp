#include "core/fps_boost.h"

#include <algorithm>
#include <cctype>

#ifdef PLATFORM_WINDOWS
#include <winsock2.h>
#include <windows.h>
#include <psapi.h>
#endif

namespace gno {
namespace fpsboost {

#ifndef PLATFORM_WINDOWS

bool setTimerResolution(std::uint32_t) { return false; }
void releaseTimerResolution() {}
std::uint32_t currentTimerResolution() { return 0; }
RamStats cleanRam() { return {}; }
bool serviceControl(const std::string&, ServiceAction) { return false; }
ServiceInfo queryService(const std::string&) { return {}; }
std::vector<StartupEntry> enumStartupPrograms() { return {}; }
bool setStartupEnabled(const std::string&, const std::string&, bool) { return false; }

#else

// ---------------------------------------------------------------- Timer

static LONG g_prev_resolution = 0;
static bool g_timer_set = false;

bool setTimerResolution(std::uint32_t hundred_ns) {
    typedef LONG (WINAPI *NtSetTimerResolution_t)(
        ULONG desired, BOOL set, PULONG current);
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) return false;
    auto setRes = reinterpret_cast<NtSetTimerResolution_t>(
       GetProcAddress(ntdll, "NtSetTimerResolution"));
    if (!setRes) return false;
    ULONG cur = 0;
    if (!setRes(hundred_ns, TRUE, &cur)) return false;
    g_prev_resolution = static_cast<LONG>(cur);
    g_timer_set = true;
    return true;
}

void releaseTimerResolution() {
    typedef LONG (WINAPI *NtSetTimerResolution_t)(
        ULONG desired, BOOL set, PULONG current);
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll || !g_timer_set) return;
    auto setRes = reinterpret_cast<NtSetTimerResolution_t>(
        GetProcAddress(ntdll, "NtSetTimerResolution"));
    if (setRes) { ULONG c; setRes(10000, FALSE, &c); } // restore default 1ms
    g_timer_set = false;
}

std::uint32_t currentTimerResolution() {
    typedef ULONG (WINAPI *NtQueryTimerResolution_t)(
        PULONG min_res, PULONG max_res, PULONG cur);
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) return 0;
    auto query = reinterpret_cast<NtQueryTimerResolution_t>(
        GetProcAddress(ntdll, "NtQueryTimerResolution"));
    if (!query) return 0;
    ULONG mn, mx, cur;
    if (query(&mn, &mx, &cur) != 0) return 0;
    return cur;
}

// ---------------------------------------------------------------- RAM Cleaner

RamStats cleanRam() {
    RamStats stats;

    // Global memory status
    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) {
        stats.total_physical = mem.ullTotalPhys;
        stats.avail_physical = mem.ullAvailPhys;
    }

    // Trim working sets of accessible processes
    DWORD pids[4096], bytes_returned = 0;
    if (!EnumProcesses(pids, sizeof(pids), &bytes_returned)) return stats;
    int count = bytes_returned / sizeof(DWORD);

    for (int i = 0; i < count; ++i) {
        HANDLE proc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_SET_QUOTA,
                                  FALSE, pids[i]);
        if (!proc) continue;
        // Skip our own process
        if (pids[i] == GetCurrentProcessId()) { CloseHandle(proc); continue; }
        SIZE_T before_ws = 0, after_ws = 0;
        GetProcessMemoryInfo(proc,
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&before_ws), 0); // dummy
        PROCESS_MEMORY_COUNTERS pmc{};
        pmc.cb = sizeof(pmc);
        if (GetProcessMemoryInfo(proc, &pmc, sizeof(pmc)))
            before_ws = pmc.WorkingSetSize;
        if (EmptyWorkingSet(proc)) {
            PROCESS_MEMORY_COUNTERS after{};
            after.cb = sizeof(after);
            if (GetProcessMemoryInfo(proc, &after, sizeof(after)))
                after_ws = after.WorkingSetSize;
            if (before_ws > after_ws)
                stats.bytes_freed_estimate += before_ws - after_ws;
            ++stats.processes_trimmed;
        }
        CloseHandle(proc);
    }

    // Purge standby list via NtSetSystemInformation
    typedef LONG (WINAPI *NtSetSystemInformation_t)(DWORD, void*, DWORD);
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (ntdll) {
        auto setInfo = reinterpret_cast<NtSetSystemInformation_t>(
            GetProcAddress(ntdll, "NtSetSystemInformation"));
        if (setInfo) {
            // SystemMemoryListInformation = 80, MemoryPurgeStandbyList = 4
            ULONG cmd = 4;
            setInfo(80, &cmd, sizeof(cmd));
        }
    }

    // Refresh memory status after cleanup
    if (GlobalMemoryStatusEx(&mem))
        stats.avail_physical = mem.ullAvailPhys;

    return stats;
}

// ---------------------------------------------------------------- Services

bool serviceControl(const std::string& name, ServiceAction action) {
    SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!scm) return false;
    SC_HANDLE svc = OpenServiceA(scm, name.c_str(), SERVICE_ALL_ACCESS);
    if (!svc) { CloseServiceHandle(scm); return false; }

    bool ok = false;
    switch (action) {
        case ServiceAction::Start:
            ok = StartServiceA(svc, 0, nullptr) != 0;
            break;
        case ServiceAction::Stop: {
            SERVICE_STATUS ss;
            ok = ControlService(svc, SERVICE_CONTROL_STOP, &ss) != 0;
            break;
        }
        case ServiceAction::Disable:
        case ServiceAction::AutoStart: {
            const DWORD type = action == ServiceAction::Disable
                ? SERVICE_DISABLED : SERVICE_AUTO_START;
            ok = ChangeServiceConfigA(svc, SERVICE_NO_CHANGE, type,
                                       SERVICE_NO_CHANGE, nullptr, nullptr,
                                       nullptr, nullptr, nullptr, nullptr, nullptr);
            break;
        }
        default: break;
    }
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return ok;
}

ServiceInfo queryService(const std::string& name) {
    ServiceInfo info;
    info.name = name;
    SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return info;
    SC_HANDLE svc = OpenServiceA(scm, name.c_str(),
                                 SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG);
    if (!svc) { CloseServiceHandle(scm); return info; }

    SERVICE_STATUS ss;
    if (QueryServiceStatus(svc, &ss)) info.running = ss.dwCurrentState == SERVICE_RUNNING;

    // Query start type
    DWORD needed = 0;
    QUERY_SERVICE_CONFIGA* config = nullptr;
    QueryServiceConfigA(svc, nullptr, 0, &needed);
    if (needed > 0 && needed < 65536) {
        std::vector<char> buf(needed);
        if (QueryServiceConfigA(svc,
            reinterpret_cast<QUERY_SERVICE_CONFIGA*>(buf.data()), needed, &needed)) {
            auto* cfg = reinterpret_cast<QUERY_SERVICE_CONFIGA*>(buf.data());
            info.start_type = static_cast<int>(cfg->dwStartType);
            info.display_name = cfg->lpDisplayName ? cfg->lpDisplayName : name;
        }
    }
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return info;
}

// ---------------------------------------------------------------- Startup Programs

std::vector<StartupEntry> enumStartupPrograms() {
    std::vector<StartupEntry> result;
    const char* keys[] = {
        "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        "Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce"
    };
    for (auto key_path : keys) {
        for (HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
            HKEY hkey;
            if (RegOpenKeyExA(root, key_path, 0, KEY_READ, &hkey) != ERROR_SUCCESS)
                continue;
            DWORD index = 0;
            char name[256];
            DWORD name_sz = sizeof(name);
            BYTE data[1024];
            DWORD data_sz = sizeof(data), data_type = 0;
            while (RegEnumValueA(hkey, index, name, &name_sz, nullptr,
                                 &data_type, data, &data_sz) == ERROR_SUCCESS) {
                StartupEntry entry;
                entry.name = name;
                entry.command = data_type == REG_SZ ? std::string(reinterpret_cast<char*>(data)) : "";
                entry.location = (root == HKEY_CURRENT_USER ? "HKCU\\" : "HKLM\\") + std::string(key_path);
                entry.enabled = true;
                result.push_back(entry);
                index++;
                name_sz = sizeof(name);
                data_sz = sizeof(data);
            }
            RegCloseKey(hkey);
        }
    }
    return result;
}

bool setStartupEnabled(const std::string& location, const std::string& name, bool enable) {
    HKEY root = location.rfind("HKCU", 0) == 0 ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
    std::string subkey = location.substr(location.find('\\') + 1);
    HKEY hkey;
    if (RegOpenKeyExA(root, subkey.c_str(), 0, KEY_SET_VALUE | KEY_QUERY_VALUE, &hkey)
        != ERROR_SUCCESS)
        return false;
    if (enable) {
        // Read the disabled value from a backup location and restore it.
        char buf[1024] = {};
        DWORD sz = sizeof(buf);
        HKEY backup;
        std::string backup_path = subkey + "_disabled";
        if (RegOpenKeyExA(root, backup_path.c_str(), 0, KEY_READ, &backup) == ERROR_SUCCESS) {
            RegQueryValueExA(backup, name.c_str(), nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(buf), &sz);
            RegCloseKey(backup);
            RegDeleteValueA(backup, name.c_str());
        }
        RegSetValueExA(hkey, name.c_str(), 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(buf), sz);
    } else {
        // Move to disabled backup
        char buf[1024] = {};
        DWORD sz = sizeof(buf);
        if (RegQueryValueExA(hkey, name.c_str(), nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(buf), &sz) != ERROR_SUCCESS) {
            RegCloseKey(hkey);
            return false;
        }
        std::string backup_path = subkey + "_disabled";
        HKEY backup_key;
        RegCreateKeyExA(root, backup_path.c_str(), 0, nullptr, 0, KEY_SET_VALUE,
                        nullptr, &backup_key, nullptr);
        RegSetValueExA(backup_key, name.c_str(), 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(buf), sz);
        RegCloseKey(backup_key);
        RegDeleteValueA(hkey, name.c_str());
    }
    RegCloseKey(hkey);
    return true;
}

#endif // PLATFORM_WINDOWS

} // namespace fpsboost
} // namespace gno
