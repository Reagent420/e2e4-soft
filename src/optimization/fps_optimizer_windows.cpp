#include "fps_optimizer_windows.h"

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#include <winreg.h>
#include <tlhelp32.h>
#include <cwchar>
#endif

namespace gno {

static std::wstring toWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring ws(len - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &ws[0], len);
    return ws;
}

bool FPSOptimizerPlatform::disableGameDVR() {
#ifdef PLATFORM_WINDOWS
    HKEY hkey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER,
                      "Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR",
                      0, KEY_SET_VALUE, &hkey) == ERROR_SUCCESS) {
        DWORD value = 0;
        RegSetValueExA(hkey, "AppCaptureEnabled", 0, REG_DWORD, (BYTE*)&value, sizeof(value));
        RegCloseKey(hkey);
        return true;
    }
#endif
    return false;
}

bool FPSOptimizerPlatform::disableFullscreenOptimizations() {
#ifdef PLATFORM_WINDOWS
    HKEY hkey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER,
                      "System\\GameConfigStore",
                      0, KEY_SET_VALUE, &hkey) == ERROR_SUCCESS) {
        DWORD value = 1;
        RegSetValueExA(hkey, "GameDVR_HonorUserFSEBehaviorMode", 0, REG_DWORD, (BYTE*)&value, sizeof(value));
        RegSetValueExA(hkey, "GameDVR_FSEBehaviorMode", 0, REG_DWORD, (BYTE*)&value, sizeof(value));
        RegCloseKey(hkey);
        return true;
    }
#endif
    return false;
}

bool FPSOptimizerPlatform::disableMouseAcceleration() {
#ifdef PLATFORM_WINDOWS
    HKEY hkey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER,
                      "Control Panel\\Mouse",
                      0, KEY_SET_VALUE, &hkey) == ERROR_SUCCESS) {
        RegSetValueExA(hkey, "MouseSpeed", 0, REG_SZ, (BYTE*)"0", 2);
        RegSetValueExA(hkey, "MouseThreshold1", 0, REG_SZ, (BYTE*)"0", 2);
        RegSetValueExA(hkey, "MouseThreshold2", 0, REG_SZ, (BYTE*)"0", 2);
        RegCloseKey(hkey);
        return true;
    }
#endif
    return false;
}

bool FPSOptimizerPlatform::optimizePowerPlan(uint32_t mode) {
#ifdef PLATFORM_WINDOWS
    (void)mode;
    return true;
#else
    return false;
#endif
}

bool FPSOptimizerPlatform::setProcessPriority(const std::string& process_name, int priority) {
#ifdef PLATFORM_WINDOWS
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    bool found = false;
    
    if (Process32FirstW(snapshot, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, toWide(process_name).c_str()) == 0) {
                HANDLE process = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pe.th32ProcessID);
                if (process) {
                    SetPriorityClass(process, HIGH_PRIORITY_CLASS);
                    CloseHandle(process);
                    found = true;
                }
                break;
            }
        } while (Process32NextW(snapshot, &pe));
    }
    
    CloseHandle(snapshot);
    return found;
#endif
    return false;
}

bool FPSOptimizerPlatform::optimizeVirtualMemory() {
#ifdef PLATFORM_WINDOWS
    HKEY hkey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management",
                      0, KEY_SET_VALUE, &hkey) == ERROR_SUCCESS) {
        DWORD value = 0;
        RegSetValueExA(hkey, "LargeSystemCache", 0, REG_DWORD, (BYTE*)&value, sizeof(value));
        RegCloseKey(hkey);
        return true;
    }
#endif
    return false;
}

bool FPSOptimizerPlatform::applyConfig(const FPSBoostConfig& config) {
    bool success = true;
    
    if (config.disable_game_dvr) {
        success &= disableGameDVR();
    }
    
    if (config.disable_fullscreen_optimizations) {
        success &= disableFullscreenOptimizations();
    }
    
    if (config.disable_mouse_acceleration) {
        success &= disableMouseAcceleration();
    }
    
    if (config.optimize_power_plan) {
        success &= optimizePowerPlan(config.power_plan_mode);
    }
    
    if (config.optimize_virtual_memory) {
        success &= optimizeVirtualMemory();
    }
    
    return success;
}

bool FPSOptimizerPlatform::revertAll() {
    return true;
}

} // namespace gno
