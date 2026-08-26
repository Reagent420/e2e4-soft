#include "core/system_manager.h"

#include <algorithm>
#include <cctype>

#ifdef PLATFORM_WINDOWS
#include <winsock2.h>
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#endif

namespace gno {
namespace sysmgr {

#ifdef PLATFORM_WINDOWS

static bool isSystemProcess(const std::string& name) {
    static const char* sys[] = {
        "system", "registry", "smss", "csrss", "wininit", "winlogon",
        "services", "lsass", "svchost", "dwm", "fontdrvhost", "sihost",
        "explorer", "conhost", "runtimebroker", "searchapp", "shellexperience",
        "wudfhost", "spoolsv", "taskhostw", "ctfmon", "audiodg"
    };
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    for (auto s : sys)
        if (lower == s) return true;
    return false;
}

std::vector<ProcInfo> enumUserProcesses() {
    std::vector<ProcInfo> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            ProcInfo info;
            info.pid = pe.th32ProcessID;
            char name[MAX_PATH] = {};
            WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1,
                                name, sizeof(name), nullptr, nullptr);

            // strip extension
            std::string n(name);
            auto dot = n.rfind('.');
            if (dot != std::string::npos) n = n.substr(0, dot);
            info.name = n;

            if (isSystemProcess(n)) { info.is_system = true; }

            HANDLE proc = OpenProcess(
                PROCESS_QUERY_INFORMATION | PROCESS_SUSPEND_RESUME,
                FALSE, pe.th32ProcessID);
            if (proc) {
                PROCESS_MEMORY_COUNTERS pmc{};
                pmc.cb = sizeof(pmc);
                GetProcessMemoryInfo(proc, &pmc, sizeof(pmc));
                info.working_set = pmc.WorkingSetSize;

                // Check if suspended: main thread wait reason = 5 (Suspended)
                // Simplified: check via NtQueryInformation would be complex.
                // For now assume not suspended unless we suspended it ourselves.
                CloseHandle(proc);
            }
            out.push_back(std::move(info));
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return out;
}

bool suspendProcess(std::uint32_t pid) {
    typedef LONG (WINAPI *NtSuspendProcess_t)(HANDLE);
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) return false;
    auto fn = reinterpret_cast<NtSuspendProcess_t>(
        GetProcAddress(ntdll, "NtSuspendProcess"));
    if (!fn) return false;
    HANDLE h = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, pid);
    if (!h) return false;
    LONG rc = fn(h);
    CloseHandle(h);
    return rc == 0;
}

bool resumeProcess(std::uint32_t pid) {
    typedef LONG (WINAPI *NtResumeProcess_t)(HANDLE);
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) return false;
    auto fn = reinterpret_cast<NtResumeProcess_t>(
        GetProcAddress(ntdll, "NtResumeProcess"));
    if (!fn) return false;
    HANDLE h = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, pid);
    if (!h) return false;
    LONG rc = fn(h);
    CloseHandle(h);
    return rc == 0;
}

bool setGpuPreference(const std::string& exe_path, int pref) {
    HKEY key = nullptr;
    const char* path =
        "Software\\Microsoft\\DirectX\\UserGpuPreferences";
    if (RegCreateKeyExA(HKEY_CURRENT_USER, path, 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return false;
    std::string val = "GpuPreference=" + std::to_string(pref) + ";";
    LSTATUS st = RegSetValueExA(key, exe_path.c_str(), 0, REG_SZ,
                                reinterpret_cast<const BYTE*>(val.c_str()),
                                static_cast<DWORD>(val.size() + 1));
    RegCloseKey(key);
    return st == ERROR_SUCCESS;
}

int getGpuPreference(const std::string& exe_path) {
    HKEY key = nullptr;
    const char* path =
        "Software\\Microsoft\\DirectX\\UserGpuPreferences";
    if (RegOpenKeyExA(HKEY_CURRENT_USER, path, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return -1;
    char buf[128] = {};
    DWORD sz = sizeof(buf);
    LSTATUS st = RegQueryValueExA(key, exe_path.c_str(), nullptr, nullptr,
                                  reinterpret_cast<LPBYTE>(buf), &sz);
    RegCloseKey(key);
    if (st != ERROR_SUCCESS) return -1;
    std::string val(buf);
    auto pos = val.find("GpuPreference=");
    if (pos == std::string::npos) return -1;
    try { return std::stoi(val.substr(pos + 14)); } catch (...) { return -1; }
}

std::string getNicDriverVersion() {
    HKEY key = nullptr;
    const char* path =
        "SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e972-e325-11ce-bfc1-08002be10318}";
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return {};
    std::string result;
    DWORD index = 0;
    char sub_name[256];
    DWORD sub_sz = sizeof(sub_name);
    while (RegEnumKeyExA(key, index, sub_name, &sub_sz, nullptr,
                         nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
        HKEY sub;
        std::string sub_path = std::string(path) + "\\" + sub_name;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, sub_path.c_str(), 0,
                          KEY_READ, &sub) == ERROR_SUCCESS) {
            char desc[256] = {}, ver[64] = {};
            DWORD d_sz = sizeof(desc), v_sz = sizeof(ver);
            if (RegQueryValueExA(sub, "DriverDesc", nullptr, nullptr,
                                 reinterpret_cast<LPBYTE>(desc), &d_sz) == ERROR_SUCCESS &&
                RegQueryValueExA(sub, "DriverVersion", nullptr, nullptr,
                                 reinterpret_cast<LPBYTE>(ver), &v_sz) == ERROR_SUCCESS) {
                result = std::string(desc) + " | " + ver;
                RegCloseKey(sub);
                break;
            }
            RegCloseKey(sub);
        }
        ++index;
        sub_sz = sizeof(sub_name);
    }
    RegCloseKey(key);
    return result;
}

#else // non-Windows stubs

std::vector<ProcInfo> enumUserProcesses() { return {}; }
bool suspendProcess(std::uint32_t) { return false; }
bool resumeProcess(std::uint32_t) { return false; }
bool setGpuPreference(const std::string&, int) { return false; }
int getGpuPreference(const std::string&) { return -1; }
std::string getNicDriverVersion() { return {}; }

#endif

} // namespace sysmgr
} // namespace gno
