#include "process_monitor.h"
#include <algorithm>
#include <thread>
#include <chrono>

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <iphlpapi.h>
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "iphlpapi.lib")
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

ProcessMonitor::ProcessMonitor() = default;
ProcessMonitor::~ProcessMonitor() { stopMonitoring(); }

std::vector<ProcessInfo> ProcessMonitor::scanProcesses() {
    std::vector<ProcessInfo> processes;

#ifdef PLATFORM_WINDOWS
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return processes;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(snapshot, &pe)) {
        do {
            ProcessInfo info;
            info.pid = pe.th32ProcessID;
            info.name = [&pe]() {
                char buf[MAX_PATH] = {};
                WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, buf, MAX_PATH, nullptr, nullptr);
                return std::string(buf);
            }();

            HANDLE proc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, info.pid);
            if (proc) {
                PROCESS_MEMORY_COUNTERS pmc;
                if (GetProcessMemoryInfo(proc, &pmc, sizeof(pmc))) {
                    info.memory_mb = static_cast<uint32_t>(pmc.WorkingSetSize / (1024 * 1024));
                }

                char path_buf[MAX_PATH] = {};
                DWORD path_len = MAX_PATH;
                if (QueryFullProcessImageNameA(proc, 0, path_buf, &path_len)) {
                    info.path = path_buf;
                }
                CloseHandle(proc);
            }

            info.total_bytes = info.bytes_sent + info.bytes_received;

            static const std::vector<std::string> game_keywords = {
                "cs2", "dota2", "valorant", "fortnite", "r5apex", "league",
                "overwatch", "tslgame", "rainbow", "warzone", "genshin",
                "wow", "rust", "javaw", "rocketleague", "deadbydaylight"
            };
            std::string lower_name = info.name;
            std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
            for (const auto& kw : game_keywords) {
                if (lower_name.find(kw) != std::string::npos) {
                    info.is_game = true;
                    break;
                }
            }

            if (info.memory_mb > 10 || info.is_game) {
                processes.push_back(info);
            }
        } while (Process32NextW(snapshot, &pe));
    }

    CloseHandle(snapshot);
#endif

    std::sort(processes.begin(), processes.end(),
              [](const ProcessInfo& a, const ProcessInfo& b) {
                  return a.memory_mb > b.memory_mb;
              });

    return processes;
}

std::vector<ProcessInfo> ProcessMonitor::getTopProcesses(int count) {
    auto all = scanProcesses();
    if (static_cast<int>(all.size()) > count) {
        all.resize(count);
    }
    return all;
}

void ProcessMonitor::setProcessCallback(ProcessCallback callback) { callback_ = std::move(callback); }

void ProcessMonitor::startMonitoring(uint32_t interval_ms) {
    if (monitoring_) return;
    monitoring_ = true;
    monitor_thread_ = std::thread([this, interval_ms]() {
        while (monitoring_) {
            auto procs = scanProcesses();
            if (callback_) callback_(procs);
            std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
        }
    });
}

void ProcessMonitor::stopMonitoring() {
    monitoring_ = false;
    if (monitor_thread_.joinable()) monitor_thread_.join();
}

} // namespace gno
