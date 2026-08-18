#include "platform_linux.h"

#ifdef PLATFORM_LINUX
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/resource.h>
#endif

namespace gno {

bool PlatformOptimizer::optimizeNetworkStack() {
#ifdef PLATFORM_LINUX
    system("sysctl -w net.ipv4.tcp_fastopen=3");
    system("sysctl -w net.ipv4.tcp_slow_start_after_idle=0");
    system("sysctl -w net.ipv4.tcp_mtu_probing=1");
    return true;
#else
    return false;
#endif
}

bool PlatformOptimizer::setProcessPriority(const std::string& process_name, int priority) {
#ifdef PLATFORM_LINUX
    std::string cmd = "renice -n " + std::to_string(priority) + " -p $(pgrep " + process_name + ")";
    return system(cmd.c_str()) == 0;
#else
    return false;
#endif
}

bool PlatformOptimizer::optimizeSystemSettings() {
    return optimizeNetworkStack();
}

} // namespace gno
