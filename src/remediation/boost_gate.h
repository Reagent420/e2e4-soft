#pragma once

// BoostGate — single entry point for "apply/revert the optimizer boost".
// P0 requirement: all system mutations from UI boost paths go through the
// transactional remediation engine (backup -> apply -> verify -> rollback).
// Anything not covered by the engine allowlist is reported as skipped, never
// written directly.

#include "remediation/remediation_types.h"

#include <string>
#include <vector>

namespace gno {
namespace remediation {

// --- Engine allowlist constants -------------------------------------------
// The engine writes exactly these values. UI must reference these constants
// instead of keeping its own (diverging) defaults: a UI default of 1400 made
// the MTU option silently dead, because boostActionsFor() only accepts 1500.
inline constexpr unsigned kEngineMtuValue = 1500;
inline constexpr const char* kEngineDnsPrimary = "1.1.1.1";
inline constexpr const char* kEngineDnsSecondary = "1.0.0.1";
// Движок пишет именно InitialRtt (HKLM\...\Tcpip\Parameters\TcpInitialRtt).
// TcpAckFrequency/TCPNoDelay из Internet Settings он не трогает — UI раньше
// показывал эти два параметра как «оптимизацию TCP», которой не было.
inline constexpr int kEngineTcpInitialRtt = 5000;
// Флаги совместимости exe, которыми движок отключает оптимизации полного экрана.
inline constexpr const char* kEngineFullscreenFlags = "~ DISABLEDXMAXIMIZEDWINDOWEDMODE";

// Mirrors what the Optimizer UI + tray boost can request.
struct BoostRequest {
    bool optimize_power_plan = true;
    bool disable_game_dvr = true;
    bool disable_fullscreen_optimizations = true; // needs a running game target
    bool disable_mouse_acceleration = true;
    bool disable_game_mode = false;
    bool set_high_priority = true;                // needs a running game target
    bool custom_dns = false;
    std::string dns_server = kEngineDnsPrimary;   // engine supports 1.1.1.1/1.0.0.1 pair
    bool tcp_optimization = false;
    bool mtu_optimization = false;                // engine value is kEngineMtuValue; custom values skipped
    unsigned mtu_value = kEngineMtuValue;
    // Known-unsafe legacy options: intentionally have no engine action.
    bool optimize_virtual_memory = false;         // HKLM, no allowlist entry -> skipped
};

struct BoostGateResult {
    bool ok = false;                 // at least one action applied or nothing to do
    bool applied = false;            // a transaction was created
    std::string transaction_id;      // empty when nothing applied
    std::string detail;              // human-readable summary/error
    std::vector<std::string> succeeded;
    std::vector<std::string> skipped;  // requested but not engine-covered (explain)
    std::vector<std::string> failed;
    // Код ошибки движка. Нужен там, где по одному тексту нельзя выбрать верную
    // формулировку: Busy — это «ничего не начинали, попробуйте позже», а не
    // «применить не удалось».
    RemediationError error = RemediationError::None;
};

// Pure mapping: which engine actions a request implies. Exposed for tests.
// has_game / has_interface gate the target-dependent actions.
std::vector<ActionId> boostActionsFor(const BoostRequest& req, bool has_game,
                                      bool has_interface, std::vector<std::string>* skipped_out);

// The one true journal location (<appdata>/GNO/Backups). The GUI, the tray and
// the console must all use this helper: if they resolve it differently,
// revertBoost() cannot see a boost applied by the other front end and the
// marker file becomes meaningless.
std::string defaultBackupsDir();

class BoostGate {
public:
    // backups_dir: JsonBackupStore directory (same one RemediationWidget uses:
    // %APPDATA%/GNO/Backups). Marker file stored there too.
    explicit BoostGate(std::string backups_dir);

    BoostGateResult applyBoost(const BoostRequest& req);
    // Apply explicit engine actions (used by auto-fix buttons); same
    // backup/verify guarantees. Actions without a resolvable target are
    // skipped, never written directly.
    BoostGateResult applyIds(const std::vector<ActionId>& ids);
    // Rolls back the transaction created by applyBoost (marker-based). When no
    // boost is active it falls back to the newest Applied transaction in the
    // journal, so single fixes applied from Diagnostics are revertible too.
    BoostGateResult revertBoost();
    // Откатывает конкретную транзакцию журнала (кнопка «Откатить выбранную»).
    // Если маркер активного буста указывает именно на неё, маркер снимается —
    // иначе трей остался бы «с бустом» после отката.
    BoostGateResult revertTransaction(const std::string& transaction_id);
    // "Буст включён" — строго про транзакцию буста (маркер). Трей использует
    // именно это, чтобы on/off не путался с одиночными фиксами.
    bool hasActiveBoost() const;
    // Есть ли вообще что откатывать: буст ИЛИ любой применённый фикс в журнале.
    // Эту проверку использует кнопка «Откатить последние изменения».
    bool hasRevertableChanges() const;
    // Новейшая применённая транзакция журнала (пусто, если такой нет).
    std::string latestAppliedTransactionId() const;

private:
    std::string marker_path() const;
    std::string backups_dir_;
};

} // namespace remediation
} // namespace gno
