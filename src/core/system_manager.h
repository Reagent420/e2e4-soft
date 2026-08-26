#pragma once

// Process management + GPU preference + NIC driver info (v3.1.0).
// Windows-only implementations; other platforms get stubs.

#include <string>
#include <vector>
#include <cstdint>

namespace gno {
namespace sysmgr {

struct ProcInfo {
    std::uint32_t pid = 0;
    std::string name;
    std::uint64_t working_set = 0;
    bool is_suspended = false;
    bool is_system = false;
};

std::vector<ProcInfo> enumUserProcesses();
bool suspendProcess(std::uint32_t pid);
bool resumeProcess(std::uint32_t pid);

// GPU Preference: 0=auto, 1=power_saving, 2=high_performance
bool setGpuPreference(const std::string& exe_path, int pref);
int getGpuPreference(const std::string& exe_path);

// NIC driver version from WMI/registry
std::string getNicDriverVersion();

} // namespace sysmgr
} // namespace gno
