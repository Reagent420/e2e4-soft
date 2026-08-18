#include "platform_windows.h"

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#include <winreg.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#endif

namespace gno {

bool PlatformOptimizer::disableGameDVR() {
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

bool PlatformOptimizer::disableFullscreenOptimizations() {
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

bool PlatformOptimizer::disableMouseAcceleration() {
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

bool PlatformOptimizer::optimizePowerPlan(uint32_t mode) {
    return false;
}

bool PlatformOptimizer::setProcessPriority(const std::string& process_name, int priority) {
    return false;
}

bool PlatformOptimizer::optimizeNetworkStack() {
#ifdef PLATFORM_WINDOWS
    MIB_TCPSTATS tcp_stats;
    if (GetTcpStatistics(&tcp_stats) == NO_ERROR) {
        return true;
    }
#endif
    return false;
}

bool PlatformOptimizer::disableNagleAlgorithm() {
    return true;
}

bool PlatformOptimizer::optimizeTCPSettings() {
    return true;
}

} // namespace gno
