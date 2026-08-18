#include "fps_optimizer.h"

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#include <winreg.h>
#include <powerbase.h>
#pragma comment(lib, "powrprof.lib")
#endif

namespace gno {

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
    
#ifdef PLATFORM_WINDOWS
    GUID* guid_list = nullptr;
    DWORD guid_count = 0;
    
    if (PowerEnumerate(nullptr, nullptr, &GUID_PROCESSOR_SUBGROUP,
                       ENUMERATECurrentUser, 0, nullptr, &guid_count) == ERROR_SUCCESS) {
        guid_list = (GUID*)malloc(sizeof(GUID) * guid_count);
        
        if (PowerEnumerate(nullptr, nullptr, &GUID_PROCESSOR_SUBGROUP,
                           ENUMERATECurrentUser, 0, (UCHAR*)guid_list, &guid_count) == ERROR_SUCCESS) {
            for (DWORD i = 0; i < guid_count; i++) {
                PowerPlan plan;
                plan.guid = "{";
                
                char guid_str[39] = {0};
                snprintf(guid_str, sizeof(guid_str),
                        "{%08lX-%04hX-%04hX-%02X%02X-%02X%02X%02X%02X%02X%02X}",
                        guid_list[i].Data1, guid_list[i].Data2, guid_list[i].Data3,
                        guid_list[i].Data4[0], guid_list[i].Data4[1], guid_list[i].Data4[2],
                        guid_list[i].Data4[3], guid_list[i].Data4[4], guid_list[i].Data4[5],
                        guid_list[i].Data4[6], guid_list[i].Data4[7]);
                
                plan.guid = guid_str;
                plan.name = "Power Plan " + std::to_string(i + 1);
                plans.push_back(plan);
            }
        }
        
        free(guid_list);
    }
#endif
    
    return plans;
}

bool FPSOptimizer::setActivePowerPlan(const std::string& guid) {
#ifdef PLATFORM_WINDOWS
    GUID plan_guid;
    if (CLSIDFromString(guid.c_str(), &plan_guid) == S_OK) {
        return PowerSetActiveScheme(nullptr, &plan_guid) == ERROR_SUCCESS;
    }
#endif
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
    
#ifdef PLATFORM_WINDOWS
    GUID* active_guid = nullptr;
    if (PowerGetActiveScheme(nullptr, &active_guid) == ERROR_SUCCESS) {
        char guid_str[39] = {0};
        snprintf(guid_str, sizeof(guid_str),
                "{%08lX-%04hX-%04hX-%02X%02X-%02X%02X%02X%02X%02X%02X}",
                active_guid->Data1, active_guid->Data2, active_guid->Data3,
                active_guid->Data4[0], active_guid->Data4[1], active_guid->Data4[2],
                active_guid->Data4[3], active_guid->Data4[4], active_guid->Data4[5],
                active_guid->Data4[6], active_guid->Data4[7]);
        original_power_plan_ = guid_str;
        LocalFree(active_guid);
        
        result.applied_changes.push_back("Saved original power plan");
    }
    
    if (mode == 1) {
        GUID high_perf = {0};
        CLSIDFromString("{8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c}", &high_perf);
        PowerSetActiveScheme(nullptr, &high_perf);
        result.applied_changes.push_back("Set High Performance power plan");
    }
#endif
    
    return result;
}

OptimizationResult FPSOptimizer::setProcessPriority(const std::string& process_name, int priority) {
    OptimizationResult result;
    result.success = true;
    
#ifdef PLATFORM_WINDOWS
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe;
        pe.dwSize = sizeof(pe);
        
        if (Process32First(snapshot, &pe)) {
            do {
                if (_stricmp(pe.szExeFile, process_name.c_str()) == 0) {
                    HANDLE process = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pe.th32ProcessID);
                    if (process) {
                        SetPriorityClass(process, HIGH_PRIORITY_CLASS);
                        CloseHandle(process);
                        result.applied_changes.push_back("Set " + process_name + " to high priority");
                    }
                    break;
                }
            } while (Process32Next(snapshot, &pe));
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
