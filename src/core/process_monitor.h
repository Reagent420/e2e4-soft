#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

namespace gno {

struct ProcessInfo {
    uint32_t pid = 0;
    std::string name;
    std::string path;
    double bytes_sent = 0.0;
    double bytes_received = 0.0;
    double total_bytes = 0.0;
    int cpu_percent = 0;
    uint32_t memory_mb = 0;
    bool is_game = false;
};

class ProcessMonitor {
public:
    ProcessMonitor();
    ~ProcessMonitor();

    std::vector<ProcessInfo> getTopProcesses(int count = 20);
    bool killProcess(uint32_t pid);
    bool blockProcess(const std::string& process_name);
    bool unblockProcess(const std::string& process_name);
    std::vector<std::string> getBlockedProcesses() const;

    using ProcessCallback = std::function<void(const std::vector<ProcessInfo>&)>;
    void setProcessCallback(ProcessCallback callback);
    void startMonitoring(uint32_t interval_ms = 2000);
    void stopMonitoring();

private:
    std::vector<ProcessInfo> scanProcesses();

    std::vector<std::string> blocked_;
    std::atomic<bool> monitoring_{false};
    std::thread monitor_thread_;
    ProcessCallback callback_;
};

} // namespace gno
