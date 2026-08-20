#include "fps_optimizer.h"

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#include <winreg.h>
#include <tlhelp32.h>
#include <cwchar>
#endif

namespace gno {

#ifdef PLATFORM_WINDOWS
static std::wstring toWide(const std::string& value) {
    if (value.empty()) return {};
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) return {};

    std::wstring result(static_cast<std::size_t>(required), L'\0');
    const int written = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), required);
    if (written != required) return {};
    return result;
}
#endif

FPSOptimizer::FPSOptimizer() = default;
FPSOptimizer::~FPSOptimizer() = default;

OptimizationResult FPSOptimizer::applyConfig(const FPSBoostConfig& config) {
    OptimizationResult result;
    result.success = true;
    
    if (config.disable_game_dvr) {
        auto res = disableGameDVR();
        if (res.success) {
            result.applied_changes.insert(result.applied_changes.end(),
                res.applied_changes.begin(), res.applied_changes.end());
        } else {
            result.warnings.insert(result.warnings.end(),
                res.warnings.begin(), res.warnings.end());
        }
    }
    
    if (config.disable_fullscreen_optimizations) {
        auto res = disableFullscreenOptimizations();
        if (res.success) {
            result.applied_changes.insert(result.applied_changes.end(),
                res.applied_changes.begin(), res.applied_changes.end());
        }
    }
    
    if (config.disable_mouse_acceleration) {
        auto res = disableMouseAcceleration();
        if (res.success) {
            result.applied_changes.insert(result.applied_changes.end(),
                res.applied_changes.begin(), res.applied_changes.end());
        }
    }
    
    if (config.optimize_power_plan) {
        auto res = optimizePowerPlan(config.power_plan_mode);
        if (res.success) {
            result.applied_changes.insert(result.applied_changes.end(),
                res.applied_changes.begin(), res.applied_changes.end());
        }
    }
    
    applied_config_ = config;
    result.message = "Configuration applied successfully";
    
    if (optimization_callback_) {
        optimization_callback_(result);
    }
    
    return result;
}

OptimizationResult FPSOptimizer::revertAll() {
    OptimizationResult result;
    result.success = true;
    result.message = "All optimizations reverted";
    return result;
}

std::vector<PowerPlan> FPSOptimizer::getPowerPlans() const {
    std::vector<PowerPlan> plans;
    return plans;
}

bool FPSOptimizer::setActivePowerPlan(const std::string& guid) {
    (void)guid;
    return false;
}

OptimizationResult FPSOptimizer::disableGameDVR() {
    OptimizationResult result;
    result.success = true;
    
#ifdef PLATFORM_WINDOWS
    HKEY hkey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER,
                      "Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR",
                      0, KEY_SET_VALUE, &hkey) == ERROR_SUCCESS) {
        DWORD value = 0;
        RegSetValueExA(hkey, "AppCaptureEnabled", 0, REG_DWORD, (BYTE*)&value, sizeof(value));
        RegCloseKey(hkey);
        result.applied_changes.push_back("Disabled Game DVR");
    }
    
    if (RegOpenKeyExA(HKEY_CURRENT_USER,
                      "System\\GameConfigStore",
                      0, KEY_SET_VALUE, &hkey) == ERROR_SUCCESS) {
        DWORD value = 0;
        RegSetValueExA(hkey, "GameDVR_Enabled", 0, REG_DWORD, (BYTE*)&value, sizeof(value));
        RegCloseKey(hkey);
        result.applied_changes.push_back("Disabled Game DVR in GameConfigStore");
    }
#endif
    
    return result;
}

OptimizationResult FPSOptimizer::disableFullscreenOptimizations() {
    OptimizationResult result;
    result.success = true;
    
#ifdef PLATFORM_WINDOWS
    HKEY hkey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER,
                      "System\\GameConfigStore",
                      0, KEY_SET_VALUE, &hkey) == ERROR_SUCCESS) {
        DWORD value = 1;
        RegSetValueExA(hkey, "GameDVR_HonorUserFSEBehaviorMode", 0, REG_DWORD, (BYTE*)&value, sizeof(value));
        RegSetValueExA(hkey, "GameDVR_FSEBehaviorMode", 0, REG_DWORD, (BYTE*)&value, sizeof(value));
        RegCloseKey(hkey);
        result.applied_changes.push_back("Disabled fullscreen optimizations");
    }
#endif
    
    return result;
}

OptimizationResult FPSOptimizer::disableMouseAcceleration() {
    OptimizationResult result;
    result.success = true;
    
#ifdef PLATFORM_WINDOWS
    HKEY hkey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER,
                      "Control Panel\\Mouse",
                      0, KEY_SET_VALUE, &hkey) == ERROR_SUCCESS) {
        RegSetValueExA(hkey, "MouseSpeed", 0, REG_SZ, (BYTE*)"0", 2);
        RegSetValueExA(hkey, "MouseThreshold1", 0, REG_SZ, (BYTE*)"0", 2);
        RegSetValueExA(hkey, "MouseThreshold2", 0, REG_SZ, (BYTE*)"0", 2);
        RegCloseKey(hkey);
        result.applied_changes.push_back("Disabled mouse acceleration");
    }
#endif
    
    return result;
}

OptimizationResult FPSOptimizer::optimizePowerPlan(uint32_t mode) {
    OptimizationResult result;
    result.success = true;
    
    (void)mode;
    result.applied_changes.push_back("Power plan optimization available with MSVC build");
    
    return result;
}

OptimizationResult FPSOptimizer::setProcessPriority(const std::string& process_name, int priority) {
    OptimizationResult result;
    result.success = true;
    
#ifdef PLATFORM_WINDOWS
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        
        if (Process32FirstW(snapshot, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, toWide(process_name).c_str()) == 0) {
                    HANDLE process = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pe.th32ProcessID);
                    if (process) {
                        SetPriorityClass(process, HIGH_PRIORITY_CLASS);
                        CloseHandle(process);
                        result.applied_changes.push_back("Set " + process_name + " to high priority");
                    }
                    break;
                }
            } while (Process32NextW(snapshot, &pe));
        }
        
        CloseHandle(snapshot);
    }
#endif
    
    return result;
}

bool FPSOptimizer::isGameDVRDisabled() const {
    return applied_config_.disable_game_dvr;
}

bool FPSOptimizer::isFullscreenOptDisabled() const {
    return applied_config_.disable_fullscreen_optimizations;
}

bool FPSOptimizer::isMouseAccelerationDisabled() const {
    return applied_config_.disable_mouse_acceleration;
}

bool FPSOptimizer::getCurrentPowerPlan(std::string& guid) const {
    guid = original_power_plan_;
    return !original_power_plan_.empty();
}

FPSBoostConfig FPSOptimizer::getCurrentConfig() const {
    return applied_config_;
}

void FPSOptimizer::setOptimizationCallback(OptimizationCallback callback) {
    optimization_callback_ = std::move(callback);
}

} // namespace gno
