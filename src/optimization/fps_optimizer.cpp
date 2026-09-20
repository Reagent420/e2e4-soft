#include "fps_optimizer.h"

#include "core/system_audit.h"

#include <sstream>

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#endif

namespace gno {

namespace {

#ifdef PLATFORM_WINDOWS
// powercfg — единственный системный вызов, оставшийся в этом классе: он
// читает/переключает схемы питания. Движок умеет ставить схему, но не умеет
// перечислять их список, а getPowerPlans нужен консольному режиму.
std::string runPowerCfg(const std::string& command) {
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0))
        return {};
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.hStdInput = nullptr;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    std::string cmdline = "powercfg.exe " + command;
    std::vector<char> cmdBuf(cmdline.begin(), cmdline.end());
    cmdBuf.push_back(0);

    // Наследуемый пайп + CREATE_NO_WINDOW: раньше процесс мог зависнуть с
    // открытым дескриптором, и ReadFile ждал вечно. Таймаут WaitForSingleObject
    // здесь не спасает (он достижим только после закрытия пайпа), поэтому
    // закрываем write-конец ДО чтения — дочерний процесс получает EOF.
    BOOL created = CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
                                  CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(writePipe);

    std::string text;
    if (created) {
        CloseHandle(pi.hThread);
        CHAR buf[4096];
        DWORD bytesRead = 0;
        while (ReadFile(readPipe, buf, sizeof(buf), &bytesRead, nullptr) && bytesRead > 0)
            text.append(buf, bytesRead);
        CloseHandle(readPipe);
        WaitForSingleObject(pi.hProcess, 15000);
        CloseHandle(pi.hProcess);
    } else {
        CloseHandle(readPipe);
    }
    return text;
}

const char* kHighPerformanceGuid = "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c";
#endif

// Перевод старого конфига в запрос движка. Поля, у которых нет аналога в
// allowlist движка, не теряются — BoostGate сам сообщает их в skipped,
// и мы выводим это как warnings: пользователь видит, что пункт не применён,
// а не тихо игнорируется.
remediation::BoostRequest toBoostRequest(const FPSBoostConfig& config) {
    remediation::BoostRequest req;
    req.disable_game_dvr = config.disable_game_dvr;
    req.disable_fullscreen_optimizations = config.disable_fullscreen_optimizations;
    req.disable_mouse_acceleration = config.disable_mouse_acceleration;
    req.disable_game_mode = config.disable_game_mode;
    req.optimize_power_plan = config.optimize_power_plan;
    req.set_high_priority = config.set_high_priority;
    // Движок намеренно не покрывает virtual memory и hardware scheduling —
    // эти флаги останутся в skipped с объяснением.
    req.optimize_virtual_memory = false;
    return req;
}

} // namespace

FPSOptimizer::FPSOptimizer(std::string backups_dir)
    : backups_dir_(std::move(backups_dir)) {}

FPSOptimizer::~FPSOptimizer() = default;

OptimizationResult FPSOptimizer::applyConfig(const FPSBoostConfig& config) {
    remediation::BoostGate gate(backups_dir_);
    const auto result = gate.applyBoost(toBoostRequest(config));

    OptimizationResult out;
    out.success = result.ok;
    out.applied_changes = result.succeeded;
    out.warnings = result.skipped;
    // failed — это реальные отказа движка, а не «движок это не умеет».
    // Они важнее warnings, поэтому идут в тот же список предупреждений.
    out.warnings.insert(out.warnings.end(), result.failed.begin(), result.failed.end());
    out.message = result.detail;
    return out;
}

OptimizationResult FPSOptimizer::revertAll() {
    remediation::BoostGate gate(backups_dir_);
    const auto result = gate.revertBoost();

    OptimizationResult out;
    out.success = result.ok;
    out.message = result.detail;
    if (result.ok)
        out.applied_changes.push_back("Reverted via transactional engine");
    return out;
}

std::vector<PowerPlan> FPSOptimizer::getPowerPlans() const {
    std::vector<PowerPlan> plans;
#ifdef PLATFORM_WINDOWS
    std::string out = runPowerCfg("/list");
    std::istringstream iss(out);
    std::string line;
    // parse lines: "Power Scheme GUID: xxxx  (name) *"
    while (std::getline(iss, line)) {
        auto g = line.find("GUID:");
        if (g == std::string::npos) continue;
        std::istringstream ls(line.substr(g + 5));
        std::string guid;
        ls >> guid;
        if (guid.empty()) continue;
        PowerPlan plan;
        plan.guid = guid;
        auto open = line.find('(');
        auto close = line.find(')');
        if (open != std::string::npos && close != std::string::npos && close > open)
            plan.name = line.substr(open + 1, close - open - 1);
        plan.is_active = line.find('*') != std::string::npos;
        plans.push_back(plan);
    }
#endif
    return plans;
}

bool FPSOptimizer::setActivePowerPlan(const std::string& guid) {
#ifdef PLATFORM_WINDOWS
    if (guid.empty()) return false;
    runPowerCfg("/setactive " + guid);
    // powercfg ничего не пишет в stdout ни при успехе, ни при ошибке —
    // поэтому проверяем фактическую активную схему, а не возвращаем true вслепую.
    return runPowerCfg("/getactivescheme").find(guid) != std::string::npos;
#else
    (void)guid;
    return false;
#endif
}

bool FPSOptimizer::isGameDVRDisabled() const {
    // Раньше возвращали applied_config_.disable_game_dvr — то есть то, что
    // мы *хотели* применить, даже если движок отказал. Теперь — реальное
    // состояние системы через тот же аудит, которым пользуется GUI.
    return SystemAudit::readGameDvrValue() == "0";
}

bool FPSOptimizer::isFullscreenOptDisabled() const {
    // «отключено» — каноническое значение SystemAudit; пусто означает
    // «игра не запущена» — тогда честно говорим, что не отключено.
    return SystemAudit::readFullscreenOptValue() == kAuditOffText;
}

bool FPSOptimizer::isMouseAccelerationDisabled() const {
    return SystemAudit::readMouseAccelValue() == kAuditOffText;
}

bool FPSOptimizer::getCurrentPowerPlan(std::string& guid) const {
    const std::string plan = SystemAudit::readActivePowerPlan();
    if (plan.empty()) return false;
    guid = plan;
    return true;
}

FPSBoostConfig FPSOptimizer::getCurrentConfig() const {
    // Конфиг по умолчанию: раньше он отражал «что мы применили», но
    // применённый конфиг теперь хранится в журнале транзакций движка.
    // Здесь отдаём безопасные значения по умолчанию — пишущие callers
    // должны идти через BoostGate.
    return FPSBoostConfig{};
}

} // namespace gno
