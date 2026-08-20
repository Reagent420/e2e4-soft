#pragma once

#include <string>
#include <vector>
#include <cstdint>

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

private:
    std::vector<ProcessInfo> scanProcesses();
};

} // namespace gno
