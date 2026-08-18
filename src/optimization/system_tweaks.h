#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace gno {

struct SystemTweak {
    std::string name;
    std::string description;
    std::string category;
    bool is_applied = false;
    bool requires_restart = false;
    std::string registry_key;
    std::string registry_value;
    uint32_t registry_dword_value = 0;
};

struct NetworkTweak {
    std::string name;
    std::string description;
    std::string command;
    bool is_applied = false;
};

class SystemTweaks {
public:
    SystemTweaks();
    ~SystemTweaks();

    std::vector<SystemTweak> getAvailableTweaks() const;
    std::vector<SystemTweak> getAppliedTweaks() const;
    
    bool applyTweak(const std::string& tweak_name);
    bool revertTweak(const std::string& tweak_name);
    bool applyAll();
    bool revertAll();

    std::vector<NetworkTweak> getNetworkTweaks() const;
    bool applyNetworkTweak(const std::string& tweak_name);
    bool revertNetworkTweak(const std::string& tweak_name);

    void scanSystemState();

private:
    void initDefaultTweaks();
    void initNetworkTweaks();
    bool applyRegistryTweak(const SystemTweak& tweak);
    bool revertRegistryTweak(const SystemTweak& tweak);

    std::vector<SystemTweak> tweaks_;
    std::vector<NetworkTweak> network_tweaks_;
};

} // namespace gno
