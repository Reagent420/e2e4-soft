#include "system_tweaks.h"

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#include <winreg.h>
#endif

namespace gno {

SystemTweaks::SystemTweaks() {
    initDefaultTweaks();
    initNetworkTweaks();
}

SystemTweaks::~SystemTweaks() = default;

void SystemTweaks::initDefaultTweaks() {
    tweaks_ = {
        {"DisableTelemetry", "Disable Windows telemetry data collection",
         "Performance", false, true,
         "HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\DataCollection",
         "AllowTelemetry", 0},
        
        {"DisableTips", "Disable Windows tips and suggestions",
         "UI", false, false,
         "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager",
         "SoftLandingEnabled", 0},
        
        {"DisableWidgets", "Disable Windows widgets",
         "UI", false, false,
         "HKLM\\SOFTWARE\\Policies\\Microsoft\\Dsh",
         "AllowNewsAndInterests", 0},
        
        {"DisableSearchBox", "Disable search box in taskbar",
         "UI", false, false,
         "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Search",
         "SearchboxTaskbarMode", 0},
        
        {"DisableLockScreen", "Disable lock screen",
         "UI", false, true,
         "HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\Personalization",
         "NoLockScreen", 1},
        
        {"DisableSleep", "Disable sleep mode",
         "Power", false, true,
         "HKLM\\SYSTEM\\CurrentControlSet\\Control\\Power",
         "HibernateEnabled", 0},
        
        {"DisableAutoPlay", "Disable AutoPlay for all drives",
         "Security", false, false,
         "HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer",
         "NoDriveTypeAutoRun", 255},
        
        {"DisableAutoUpdate", "Disable automatic Windows Update",
         "Performance", false, true,
         "HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate\\AU",
         "NoAutoUpdate", 1},
        
        {"OptimizeNetworking", "Optimize network stack for gaming",
         "Network", false, true,
         "HKLM\\SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters",
         "TcpAckFrequency", 1},
        
        {"DisableNagle", "Disable Nagle's algorithm",
         "Network", false, false,
         "HKLM\\SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters",
         "TcpNoDelay", 1},
        
        {"OptimizeMemory", "Optimize virtual memory settings",
         "Performance", false, true,
         "HKLM\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management",
         "LargeSystemCache", 0}
    };
}

void SystemTweaks::initNetworkTweaks() {
    network_tweaks_ = {
        {"OptimizeTCP", "Optimize TCP parameters for gaming",
         "netsh int tcp set global autotuninglevel=normal", false},
        
        {"DisableECN", "Disable ECN capability",
         "netsh int tcp set global ecncapability=disabled", false},
        
        {"EnableRSS", "Enable Receive Side Scaling",
         "netsh int tcp set global rss=enabled", false},
        
        {"OptimizeBuffer", "Optimize receive buffer size",
         "netsh int tcp set global chimney=enabled", false},
        
        {"DisableTimestamps", "Disable TCP timestamps",
         "netsh int tcp set global timestamps=disabled", false},
        
        {"OptimizeCongestion", "Optimize congestion control",
         "netsh int tcp set supplemental template=Internet congestionprovider=ctcp", false}
    };
}

std::vector<SystemTweak> SystemTweaks::getAvailableTweaks() const {
    return tweaks_;
}

std::vector<SystemTweak> SystemTweaks::getAppliedTweaks() const {
    std::vector<SystemTweak> applied;
    for (const auto& tweak : tweaks_) {
        if (tweak.is_applied) {
            applied.push_back(tweak);
        }
    }
    return applied;
}

bool SystemTweaks::applyTweak(const std::string& tweak_name) {
    for (auto& tweak : tweaks_) {
        if (tweak.name == tweak_name && !tweak.is_applied) {
            if (applyRegistryTweak(tweak)) {
                tweak.is_applied = true;
                return true;
            }
        }
    }
    return false;
}

bool SystemTweaks::revertTweak(const std::string& tweak_name) {
    for (auto& tweak : tweaks_) {
        if (tweak.name == tweak_name && tweak.is_applied) {
            if (revertRegistryTweak(tweak)) {
                tweak.is_applied = false;
                return true;
            }
        }
    }
    return false;
}

bool SystemTweaks::applyAll() {
    bool all_applied = true;
    for (const auto& tweak : tweaks_) {
        if (!tweak.is_applied) {
            if (!applyTweak(tweak.name)) {
                all_applied = false;
            }
        }
    }
    return all_applied;
}

bool SystemTweaks::revertAll() {
    bool all_reverted = true;
    for (const auto& tweak : tweaks_) {
        if (tweak.is_applied) {
            if (!revertTweak(tweak.name)) {
                all_reverted = false;
            }
        }
    }
    return all_reverted;
}

std::vector<NetworkTweak> SystemTweaks::getNetworkTweaks() const {
    return network_tweaks_;
}

bool SystemTweaks::applyNetworkTweak(const std::string& tweak_name) {
    for (auto& tweak : network_tweaks_) {
        if (tweak.name == tweak_name && !tweak.is_applied) {
            int result = system(tweak.command.c_str());
            if (result == 0) {
                tweak.is_applied = true;
                return true;
            }
        }
    }
    return false;
}

bool SystemTweaks::revertNetworkTweak(const std::string& tweak_name) {
    for (auto& tweak : network_tweaks_) {
        if (tweak.name == tweak_name && tweak.is_applied) {
            tweak.is_applied = false;
            return true;
        }
    }
    return false;
}

void SystemTweaks::scanSystemState() {
    for (auto& tweak : tweaks_) {
        tweak.is_applied = false;
    }
    
    for (auto& tweak : network_tweaks_) {
        tweak.is_applied = false;
    }
}

bool SystemTweaks::applyRegistryTweak(const SystemTweak& tweak) {
#ifdef PLATFORM_WINDOWS
    HKEY hkey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, tweak.registry_key.c_str(),
                      0, KEY_SET_VALUE, &hkey) == ERROR_SUCCESS) {
        if (tweak.registry_dword_value != 0 || tweak.registry_value == "0") {
            DWORD value = tweak.registry_dword_value;
            RegSetValueExA(hkey, tweak.registry_value.c_str(), 0, REG_DWORD,
                          (BYTE*)&value, sizeof(value));
        } else {
            DWORD value = 0;
            RegSetValueExA(hkey, tweak.registry_value.c_str(), 0, REG_DWORD,
                          (BYTE*)&value, sizeof(value));
        }
        RegCloseKey(hkey);
        return true;
    }
#endif
    return false;
}

bool SystemTweaks::revertRegistryTweak(const SystemTweak& tweak) {
#ifdef PLATFORM_WINDOWS
    HKEY hkey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, tweak.registry_key.c_str(),
                      0, KEY_SET_VALUE, &hkey) == ERROR_SUCCESS) {
        DWORD value = 1;
        RegSetValueExA(hkey, tweak.registry_value.c_str(), 0, REG_DWORD,
                      (BYTE*)&value, sizeof(value));
        RegCloseKey(hkey);
        return true;
    }
#endif
    return false;
}

} // namespace gno
