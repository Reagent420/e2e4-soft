#pragma once

// FPSOptimizer — тонкая обёртка над транзакционным движком remediation::BoostGate.
//
// ИСТОРИЯ: раньше этот класс был второй, параллельной системой оптимизации —
// писал в реестр и дёргал powercfg напрямую, в обход транзакционного движка.
// У него не было ни бэкапа, ни отката, ни проверки результата. GUI и консольный
// --game уже давно перешли на BoostGate; здесь оставался только прямой вызов
// мёртвого кода. Теперь обёртка переводит старый FPSBoostConfig в BoostRequest
// и делегирует движку, а самостоятельно системные настройки не трогает.
//
// Все прямые registry/powercfg-вызовы удалены. Читать состояние системы
// можно через SystemAudit — именно его использует GUI.

#include "remediation/boost_gate.h"

#include <string>
#include <vector>
#include <cstdint>

namespace gno {

// Результат оптимизации — человекочитаемая сводка движка.
struct OptimizationResult {
    bool success = false;
    std::string message;
    std::vector<std::string> applied_changes;
    std::vector<std::string> warnings;
};

// Старый конфиг FPS-буста. Сохранён ради совместимости консольных вызывающих;
// смысл полей тот же, что у BoostRequest.
struct FPSBoostConfig {
    bool disable_game_dvr = true;
    bool disable_hardware_scheduling = false;   // нет аналога в движке → skipped
    bool disable_fullscreen_optimizations = true;
    bool disable_game_mode = false;
    bool disable_windowsTips = true;            // нет аналога в движке → skipped
    bool optimize_power_plan = true;
    bool set_high_priority = true;
    bool disable_nvidia_latency = false;        // нет аналога в движке → skipped
    bool disable_mouse_acceleration = true;
    bool optimize_virtual_memory = true;        // движок намеренно не покрывает → skipped
    uint32_t power_plan_mode = 1;                // всегда High Performance
};

struct PowerPlan {
    std::string name;
    std::string guid;
    bool is_active = false;
};

class FPSOptimizer {
public:
    // backups_dir передаётся в BoostGate; по умолчанию — общий каталог движка,
    // чтобы откат, сделанный из GUI, был виден и отсюда.
    explicit FPSOptimizer(std::string backups_dir = remediation::defaultBackupsDir());
    ~FPSOptimizer();

    FPSOptimizer(const FPSOptimizer&) = delete;
    FPSOptimizer& operator=(const FPSOptimizer&) = delete;

    // Применяет конфиг через транзакционный движок (backup → apply → verify).
    OptimizationResult applyConfig(const FPSBoostConfig& config);

    // Откат последнего буста через движок (маркер + журнал).
    OptimizationResult revertAll();

    // --- Только чтение -----------------------------------------------------
    // Схемы питания — единственное чтение, которому движок не предоставляет
    // аналога (PowerPlanValue — это GUID активной схемы, а не список).
    std::vector<PowerPlan> getPowerPlans() const;
    bool setActivePowerPlan(const std::string& guid);

    // Реальное состояние системы, а не применённый нами конфиг.
    // Эти методы раньше возвращали applied_config_ — то есть то, что мы
    // *хотели* применить, даже если ничего не вышло. Теперь они читают
    // систему через SystemAudit.
    bool isGameDVRDisabled() const;
    bool isFullscreenOptDisabled() const;
    bool isMouseAccelerationDisabled() const;
    bool getCurrentPowerPlan(std::string& guid) const;

    FPSBoostConfig getCurrentConfig() const;

private:
    std::string backups_dir_;
};

} // namespace gno
