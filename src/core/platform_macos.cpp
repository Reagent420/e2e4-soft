#include "platform_macos.h"

#ifdef PLATFORM_MACOS
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

namespace gno {

bool PlatformOptimizer::optimizeNetworkStack() {
#ifdef PLATFORM_MACOS
    system("sysctl -w net.inet.tcp.fastopen=3");
    system("sysctl -w net.inet.tcp.slowstart_initial_idle=0");
    return true;
#else
    return false;
#endif
}

bool PlatformOptimizer::setProcessPriority(const std::string& process_name, int priority) {
#ifdef PLATFORM_MACOS
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
