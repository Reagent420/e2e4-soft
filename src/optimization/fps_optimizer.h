#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <functional>

namespace gno {

struct OptimizationResult {
    bool success = false;
    std::string message;
    std::vector<std::string> applied_changes;
    std::vector<std::string> warnings;
};

struct FPSBoostConfig {
    bool disable_game_dvr = false;
    bool disable_hardware_scheduling = false;
    bool disable_fullscreen_optimizations = false;
    bool disable_game_mode = false;
    bool disable_windowsTips = false;
    bool optimize_power_plan = false;
    bool set_high_priority = false;
    bool disable_nvidia_latency = false;
    bool disable_mouse_acceleration = false;
    bool optimize_virtual_memory = false;
    uint32_t power_plan_mode = 1;
};

struct PowerPlan {
    std::string name;
    std::string guid;
    bool is_active = false;
};

class FPSOptimizer {
public:
    FPSOptimizer();
    ~FPSOptimizer();

    OptimizationResult applyConfig(const FPSBoostConfig& config);
    OptimizationResult revertAll();

    std::vector<PowerPlan> getPowerPlans() const;
    bool setActivePowerPlan(const std::string& guid);
    
    OptimizationResult disableGameDVR();
    OptimizationResult disableFullscreenOptimizations();
    OptimizationResult disableMouseAcceleration();
    OptimizationResult optimizePowerPlan(uint32_t mode);
    OptimizationResult setProcessPriority(const std::string& process_name, int priority);
    
    bool isGameDVRDisabled() const;
    bool isFullscreenOptDisabled() const;
    bool isMouseAccelerationDisabled() const;
    bool getCurrentPowerPlan(std::string& guid) const;

    FPSBoostConfig getCurrentConfig() const;
    
    using OptimizationCallback = std::function<void(const OptimizationResult&)>;
    void setOptimizationCallback(OptimizationCallback callback);

private:
    FPSBoostConfig applied_config_;
    std::string original_power_plan_;
    OptimizationCallback optimization_callback_;
};

} // namespace gno
