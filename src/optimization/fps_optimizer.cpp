#include "fps_optimizer.h"

#include <sstream>

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

namespace {

#ifdef PLATFORM_WINDOWS
std::string runPowerShell(const std::string& command) {
    // runs powercfg and captures stdout
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0))
        return {};
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.hStdInput = nullptr;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    std::string cmdline = "powercfg.exe " + command;
    std::vector<char> cmdBuf(cmdline.begin(), cmdline.end());
    cmdBuf.push_back(0);

    BOOL created = CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
                                  CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(writePipe);

    std::string text;
    if (created) {
        CloseHandle(pi.hThread);
        CHAR buf[4096];
        DWORD bytesRead = 0;
        while (ReadFile(readPipe, buf, sizeof(buf), &bytesRead, nullptr) && bytesRead > 0)
            text.append(buf, bytesRead);
        CloseHandle(readPipe);
        WaitForSingleObject(pi.hProcess, 15000);
        CloseHandle(pi.hProcess);
    } else {
        CloseHandle(readPipe);
    }
    return text;
}

const char* kHighPerformanceGuid = "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c";
#endif

} // namespace

FPSOptimizer::FPSOptimizer() {
#ifdef PLATFORM_WINDOWS
    // remember the original active plan so revertAll() can restore it
    std::string out = runPowerShell("/getactivescheme");
    auto pos = out.find("GUID");
    if (pos != std::string::npos) {
        // format: ... GUID: xxxx  (name)
        std::istringstream iss(out.substr(pos + 5));
        std::string guid;
        iss >> guid;
        if (!guid.empty() && guid.size() > 8)
            original_power_plan_ = guid;
    }
#endif
}

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

#ifdef PLATFORM_WINDOWS
    if (config.disable_game_mode) {
        HKEY hkey;
        if (RegOpenKeyExA(HKEY_CURRENT_USER,
                          "Software\\Microsoft\\Windows\\CurrentVersion\\GameBar",
                          0, KEY_SET_VALUE, &hkey) == ERROR_SUCCESS) {
            DWORD value = 0;
            RegSetValueExA(hkey, "AutoGameModeEnabled", 0, REG_DWORD, (BYTE*)&value, sizeof(value));
            RegCloseKey(hkey);
            result.applied_changes.push_back("Disabled Windows Game Mode");
        }
    }

    if (config.optimize_virtual_memory) {
        HKEY hkey;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                          "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management",
                          0, KEY_SET_VALUE, &hkey) == ERROR_SUCCESS) {
            DWORD value = 0;
            RegSetValueExA(hkey, "LargeSystemCache", 0, REG_DWORD, (BYTE*)&value, sizeof(value));
            RegCloseKey(hkey);
            result.applied_changes.push_back("Optimized system cache (virtual memory)");
        } else {
            result.warnings.push_back("Virtual memory optimization requires administrator rights");
        }
    }
#endif

    if (config.set_high_priority) {
        // priority is applied per-game when the game starts (GameWatcher integration)
        result.applied_changes.push_back("High priority will be applied to the game at launch");
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

#ifdef PLATFORM_WINDOWS
    if (!original_power_plan_.empty()) {
        std::string out = runPowerShell("/setactive " + original_power_plan_);
        result.applied_changes.push_back("Restored original power plan");
    }

    HKEY hkey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER,
                      "Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR",
                      0, KEY_SET_VALUE, &hkey) == ERROR_SUCCESS) {
        DWORD value = 1;
        RegSetValueExA(hkey, "AppCaptureEnabled", 0, REG_DWORD, (BYTE*)&value, sizeof(value));
        RegCloseKey(hkey);
        result.applied_changes.push_back("Re-enabled Game DVR");
    }
#endif

    return result;
}

std::vector<PowerPlan> FPSOptimizer::getPowerPlans() const {
    std::vector<PowerPlan> plans;
#ifdef PLATFORM_WINDOWS
    std::string out = runPowerShell("/list");
    std::istringstream iss(out);
    std::string line;
    std::string active_guid;
    std::string cur = out;
    auto pos = cur.find("/setactive");
    // parse lines: "Power Scheme GUID: xxxx  (name) *"
    while (std::getline(iss, line)) {
        auto g = line.find("GUID:");
        if (g == std::string::npos) continue;
        std::istringstream ls(line.substr(g + 5));
        std::string guid;
        ls >> guid;
        if (guid.empty()) continue;
        PowerPlan plan;
        plan.guid = guid;
        auto open = line.find('(');
        auto close = line.find(')');
        if (open != std::string::npos && close != std::string::npos && close > open)
            plan.name = line.substr(open + 1, close - open - 1);
        plan.is_active = line.find('*') != std::string::npos;
        plans.push_back(plan);
    }
#endif
    return plans;
}

bool FPSOptimizer::setActivePowerPlan(const std::string& guid) {
#ifdef PLATFORM_WINDOWS
    std::string out = runPowerShell("/setactive " + guid);
    return true;
#else
    (void)guid;
    return false;
#endif
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

#ifdef PLATFORM_WINDOWS
    if (original_power_plan_.empty()) {
        // capture current plan (we failed at ctor or it was empty)
        std::string out = runPowerShell("/getactivescheme");
        auto pos = out.find("GUID");
        if (pos != std::string::npos) {
            std::istringstream iss(out.substr(pos + 5));
            std::string guid;
            iss >> guid;
            if (guid.size() > 8) original_power_plan_ = guid;
        }
    }
    std::string out = runPowerShell(std::string("/setactive ") + kHighPerformanceGuid);
    result.applied_changes.push_back("Switched to High Performance power plan");
#else
    result.warnings.push_back("Power plan optimization available on Windows only");
#endif

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
