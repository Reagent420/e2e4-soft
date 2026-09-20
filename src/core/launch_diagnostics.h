#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace gno {

struct DiagnosticCheck {
    std::string category;     // "Сеть", "Система", "FPS", "Игра"
    std::string name;         // check name
    bool passed = false;      // true = ok
    std::string detail;       // what was measured/seen
    std::string explanation;  // how the program sees it ("как программа это видит")
    std::string recommendation; // what the user can do
    std::string fix_action;   // id of a fix the program can run (empty if manual)
    int severity = 0;         // 0 ok, 1 warning, 2 error
};

struct GameDiagnostics {
    std::string game_name;
    std::string process_name;
    bool elevated = false;
    uint32_t timestamp = 0;
    std::vector<DiagnosticCheck> checks;
    int passed_count = 0;
    int warning_count = 0;
    int error_count = 0;
};

// Diagnoses why a game may fail to launch or perform badly. Pure C++ (no Qt).
class LaunchDiagnostics {
public:
    static GameDiagnostics run(const std::string& game_name, const std::string& process_name = "");

    // Individual checks, exposed for reuse/tests
    static DiagnosticCheck checkInternetConnectivity();
    static DiagnosticCheck checkDnsResolution();
    static DiagnosticCheck checkMtu();
    static DiagnosticCheck checkPowerPlan();
    static DiagnosticCheck checkGameDvr();
    static DiagnosticCheck checkFullscreenOptimizations();
    static DiagnosticCheck checkDiskSpace();
    static DiagnosticCheck checkRam();
    static DiagnosticCheck checkConflictingProcesses();
    static DiagnosticCheck checkGameProcess(const std::string& process_name);
    static DiagnosticCheck checkRuntimeLibraries();

    // Applies a fix by action id; returns description of what was done.
    static std::string applyFix(const std::string& action_id);

    // Результат применения с машинно-читаемым признаком успеха. Нужен там, где
    // по тексту сообщения нельзя понять, изменилось ли что-то на самом деле
    // (автопилот при запуске игры).
    struct FixOutcome {
        bool applied = false;       // изменение реально внесено движком
        bool informational = false; // не ошибка, но и не применение (подсказка/намеренный отказ)
        std::string message;        // человекочитаемый текст для UI
    };
    static FixOutcome applyFixDetailed(const std::string& action_id);

    // Применяет выбранный пользователем DNS через транзакционный движок.
    // applyFixDetailed("dns") ставит пару движка 1.1.1.1/1.0.0.1, а эта перегрузка —
    // те серверы, что выбраны в таблице DNS (или сброс на DHCP при automatic=true).
    // Адреса отвалидированы заранее: они подставляются в netsh-команду.
    static FixOutcome applyDnsDetailed(const std::vector<std::string>& servers, bool automatic = false);

private:
    static void add(GameDiagnostics& d, DiagnosticCheck c);
};

} // namespace gno