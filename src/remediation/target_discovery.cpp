#include "remediation/target_discovery.h"

#include "core/game_profiles.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

#ifdef PLATFORM_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace gno {
namespace remediation {

#ifdef PLATFORM_WINDOWS

std::optional<ActionTarget> discoverPrimaryInterface() {
    ULONG size = 15000;
    std::vector<char> buffer(size);
    auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, nullptr, adapters, &size) ==
        ERROR_BUFFER_OVERFLOW) {
        buffer.resize(size);
        adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    }
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, nullptr, adapters, &size) != NO_ERROR)
        return std::nullopt;

    auto make_target = [](IP_ADAPTER_ADDRESSES* adapter) {
        char guid[128] = {};
        strncpy(guid, adapter->AdapterName, sizeof(guid) - 1);
        return ActionTarget{InterfaceTarget{guid, adapter->IfIndex}};
    };

    // Первый проход — настоящий адаптер (Ethernet/Wi-Fi). Туннельные и loopback
    // пропускаем: у туннеля MTU из MIB бывает 65535, из-за чего движок падал с
    // «observed state out of bounds» и не применял НИЧЕГО, а выставлять MTU/DNS
    // виртуальному адаптеру бессмысленно — трафик идёт через сам туннель.
    IP_ADAPTER_ADDRESSES* fallback = nullptr;
    for (auto* adapter = adapters; adapter; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp || !adapter->FirstGatewayAddress) continue;
        const bool virtual_like = adapter->IfType == IF_TYPE_TUNNEL ||
                                  adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK;
        if (virtual_like) {
            // VPN-only конфигурация: если ничего другого нет — используем туннель.
            if (!fallback) fallback = adapter;
            continue;
        }
        return make_target(adapter);
    }
    if (fallback) return make_target(fallback);
    return std::nullopt;
}

std::optional<DiscoveredGame> discoverRunningGameProcess() {
    GameProfiles profiles;
    std::vector<std::string> names;
    for (const auto& p : profiles.getAll())
        if (!p.process_name.empty()) names.push_back(p.process_name);
    if (names.empty()) return std::nullopt;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return std::nullopt;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    std::optional<DiscoveredGame> found;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            char name[MAX_PATH] = {};
            WideCharToMultiByte(CP_UTF8, 0, entry.szExeFile, -1, name, sizeof(name), nullptr, nullptr);
            const std::string base = std::filesystem::path(name).stem().string();
            for (const auto& candidate : names) {
                if (_stricmp(base.c_str(), candidate.c_str()) != 0) continue;

                HANDLE handle =
                    OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
                if (!handle) break;
                FILETIME created{}, exited{}, kernel{}, user{};
                char path[4096] = {};
                DWORD path_size = sizeof(path);
                DiscoveredGame game;
                game.pid = entry.th32ProcessID;
                if (GetProcessTimes(handle, &created, &exited, &kernel, &user))
                    game.creation_time =
                        (static_cast<std::uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
                QueryFullProcessImageNameA(handle, 0, path, &path_size);
                game.path = path;
                CloseHandle(handle);
                if (game.creation_time != 0 && !game.path.empty()) found = game;
                break;
            }
        } while (!found && Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

#else

std::optional<ActionTarget> discoverPrimaryInterface() { return std::nullopt; }
std::optional<DiscoveredGame> discoverRunningGameProcess() { return std::nullopt; }

#endif

} // namespace remediation
} // namespace gno
