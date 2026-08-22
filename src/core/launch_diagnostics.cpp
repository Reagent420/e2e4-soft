#include "launch_diagnostics.h"

#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <sstream>
#include <vector>
#include <thread>
#include <cstdlib>
#include <filesystem>

#include "remediation/legacy_bridge.h"
#include "remediation/windows_state_api.h"
#include "remediation/backup_store.h"

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#include <winreg.h>
#include <iphlpapi.h>
#include <tlhelp32.h>
#include <sysinfoapi.h>
#include <psapi.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "ws2_32.lib")
#endif

#include "network_utils.h"
#include "system_audit.h"

namespace gno {

namespace {

#ifdef PLATFORM_WINDOWS
std::string runCmd(const std::string& cmdline) {
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

    std::string cmd = "cmd.exe /c " + cmdline;
    std::vector<char> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(0);

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

bool pingHost(const char* host) {
    std::string out = runCmd(std::string("ping -n 3 -w 1000 ") + host);
    return out.find("TTL=") != std::string::npos || out.find("TTL =") != std::string::npos ||
           out.find("time=") != std::string::npos || out.find("time<") != std::string::npos;
}

bool isProcessRunning(const std::string& name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    bool found = false;
    std::wstring target(name.begin(), name.end());
    for (bool ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        std::wstring exe(pe.szExeFile);
        if (exe == target) { found = true; break; }
    }
    CloseHandle(snap);
    return found;
}
#endif

} // namespace

void LaunchDiagnostics::add(GameDiagnostics& d, DiagnosticCheck c) {
    d.checks.push_back(c);
    if (c.severity == 2) ++d.error_count;
    else if (c.severity == 1) ++d.warning_count;
    else ++d.passed_count;
}

GameDiagnostics LaunchDiagnostics::run(const std::string& game_name, const std::string& process_name) {
    GameDiagnostics d;
    d.game_name = game_name;
    d.process_name = process_name;
    d.elevated = SystemAudit::isAdmin();
    d.timestamp = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    add(d, checkInternetConnectivity());
    add(d, checkDnsResolution());
    add(d, checkMtu());
    add(d, checkPowerPlan());
    add(d, checkGameDvr());
    add(d, checkFullscreenOptimizations());
    add(d, checkDiskSpace());
    add(d, checkRam());
    add(d, checkConflictingProcesses());
    add(d, checkGameProcess(process_name));
    add(d, checkRuntimeLibraries());

    return d;
}

DiagnosticCheck LaunchDiagnostics::checkInternetConnectivity() {
    DiagnosticCheck c;
    c.category = "Сеть";
    c.name = "Доступ в интернет";
    c.severity = 2;
#ifdef PLATFORM_WINDOWS
    bool ok = pingHost("8.8.8.8") || pingHost("1.1.1.1");
    c.passed = ok;
    c.detail = ok ? "Программа выполнила ping до 8.8.8.8 и 1.1.1.1 — ответ получен."
                  : "Программа выполнила ping до 8.8.8.8 и 1.1.1.1 — ответа нет.";
    c.explanation = "Программа отправляет ICMP-пакеты и ждёт ответ. Это самый прямой способ понять, есть ли у компьютера интернет вообще.";
    c.recommendation = ok ? "Интернет работает — проверяем следующие шаги."
                          : "Нет соединения: проверьте кабель/Wi-Fi, отключите VPN-клиенты и брандмауэры, перезагрузите роутер.";
#else
    c.passed = true;
    c.detail = "Не проверяется на этой платформе.";
    c.explanation = "Диагностика сети работает на Windows.";
    c.recommendation = "";
#endif
    return c;
}

DiagnosticCheck LaunchDiagnostics::checkDnsResolution() {
    DiagnosticCheck c;
    c.category = "Сеть";
    c.name = "DNS-резолвинг";
    c.severity = 1;
#ifdef PLATFORM_WINDOWS
    std::string out = runCmd("nslookup -timeout=3 google.com 2>nul");
    bool ok = out.find("Name:") != std::string::npos || out.find("Address") != std::string::npos;
    c.passed = ok;
    c.detail = ok ? "Программа выполнила nslookup google.com — домен разрешился в IP."
                  : "Программа выполнила nslookup google.com — домен не разрешился.";
    c.explanation = "Программа спрашивает у DNS-сервера адрес домена. Если ответа нет — DNS сломан или заблокирован, игра не сможет подключиться к серверам.";
    c.recommendation = ok ? ""
                          : "Смените DNS-сервер на вкладке «Оптимизация» (например, 1.1.1.1 или 8.8.8.8) — программа сделает это через netsh.";
    c.fix_action = "dns";
#else
    c.passed = true;
    c.detail = "Не проверяется на этой платформе.";
    c.explanation = "Диагностика сети работает на Windows.";
    c.recommendation = "";
#endif
    return c;
}

DiagnosticCheck LaunchDiagnostics::checkMtu() {
    DiagnosticCheck c;
    c.category = "Сеть";
    c.name = "MTU сетевого адаптера";
    c.severity = 1;
#ifdef PLATFORM_WINDOWS
    uint32_t mtu = 0;
    bool ok = NetworkUtils::getMTU(NetworkUtils::getNetworkInterfaceName(), mtu);
    c.passed = ok && mtu <= 1500 && mtu >= 576;
    c.detail = ok ? "Программа запросила MTU через netsh — сейчас " + std::to_string(mtu) + "."
                  : "Программа не смогла прочитать MTU (нужны права администратора).";
    c.explanation = "MTU — максимальный размер пакета. Слишком большой MTU вызывает фрагментацию и потери в играх. Программа читает его командой netsh ipv4 show subinterfaces.";
    c.recommendation = ok && mtu == 1500
        ? "MTU = 1500 (стандартный). Для игр программа может установить 1400 — меньше фрагментация на плохих линиях."
        : "Установите MTU 1400 на вкладке «Оптимизация» — программа сделает это через netsh.";
    c.fix_action = "mtu";
#else
    c.passed = true;
    c.detail = "Не проверяется на этой платформе.";
    c.explanation = "Диагностика сети работает на Windows.";
    c.recommendation = "";
#endif
    return c;
}

DiagnosticCheck LaunchDiagnostics::checkPowerPlan() {
    DiagnosticCheck c;
    c.category = "FPS";
    c.name = "План питания";
    c.severity = 1;
    std::string plan = SystemAudit::readActivePowerPlan();
    bool highPerf = plan == "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c";
    c.passed = highPerf;
    c.detail = highPerf
        ? "Программа прочитала ActivePowerScheme — активен план «Высокая производительность»."
        : "Программа прочитала ActivePowerScheme — активен другой план (" + (plan.empty() ? "не определён" : plan) + ").";
    c.explanation = "План питания ограничивает частоту процессора. На «Сбалансированном» плане CPU сбрасывает частоту — в играх падает FPS. Программа читает активный план из реестра Windows.";
    c.recommendation = highPerf ? ""
                                : "Примените план «Высокая производительность» на вкладке «Оптимизация» — программа переключит его через powercfg.";
    c.fix_action = "power_plan";
    return c;
}

DiagnosticCheck LaunchDiagnostics::checkGameDvr() {
    DiagnosticCheck c;
    c.category = "FPS";
    c.name = "Запись игр (Game DVR)";
    c.severity = 1;
    std::string val = SystemAudit::readGameDvrValue();
    bool off = val == "0";
    c.passed = off;
    c.detail = off ? "Программа видит AppCaptureEnabled = 0 — запись игр отключена."
                   : "Программа видит AppCaptureEnabled = " + (val.empty() ? "не задан" : val) + " — запись игр может работать в фоне.";
    c.explanation = "Game DVR пишет видео с экрана в фоне и забирает FPS. Программа читает параметр AppCaptureEnabled в реестре текущего пользователя.";
    c.recommendation = off ? "" : "Отключите запись игр на вкладке «Оптимизация».";
    c.fix_action = "game_dvr";
    return c;
}

DiagnosticCheck LaunchDiagnostics::checkFullscreenOptimizations() {
    DiagnosticCheck c;
    c.category = "FPS";
    c.name = "Оптимизации полноэкранного режима";
    c.severity = 0;
    std::string val = SystemAudit::readFullscreenOptValue();
    bool off = val == "1";
    c.passed = off;
    c.detail = off ? "Программа видит GameDVR_FSEBehaviorMode = 1 — оптимизации полноэкранного режима отключены."
                   : "Программа видит GameDVR_FSEBehaviorMode = " + (val.empty() ? "не задан" : val) + ".";
    c.explanation = "Оптимизации полноэкранного режима добавляют задержку ввода в некоторых играх. Программа читает параметр из реестра.";
    c.recommendation = off ? "" : "Отключите оптимизации полноэкранного режима на вкладке «Оптимизация».";
    c.fix_action = "fullscreen_opt";
    return c;
}

DiagnosticCheck LaunchDiagnostics::checkDiskSpace() {
    DiagnosticCheck c;
    c.category = "Система";
    c.name = "Свободное место на диске";
    c.severity = 1;
#ifdef PLATFORM_WINDOWS
    ULARGE_INTEGER freeAvail, total;
    if (GetDiskFreeSpaceExA(nullptr, &freeAvail, &total, nullptr)) {
        double gb = static_cast<double>(freeAvail.QuadPart) / (1024.0 * 1024.0 * 1024.0);
        bool ok = gb > 5.0;
        c.passed = ok;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.1f", gb);
        c.detail = ok ? "Программа запросила свободное место — " + std::string(buf) + " ГБ."
                      : "Свободно всего " + std::string(buf) + " ГБ.";
        c.explanation = "Играм нужно место для кэша и обновлений. Программа читает свободное место через Windows API.";
        c.recommendation = ok ? "" : "Освободите место на диске (нужно минимум 5 ГБ).";
    } else {
        c.passed = true;
        c.detail = "Не удалось прочитать диск.";
        c.explanation = "Программа не смогла запросить место на диске.";
        c.recommendation = "";
    }
#else
    c.passed = true;
    c.detail = "Не проверяется на этой платформе.";
    c.explanation = "";
    c.recommendation = "";
#endif
    return c;
}

DiagnosticCheck LaunchDiagnostics::checkRam() {
    DiagnosticCheck c;
    c.category = "Система";
    c.name = "Оперативная память";
    c.severity = 1;
#ifdef PLATFORM_WINDOWS
    MEMORYSTATUSEX mem;
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) {
        double freeGB = static_cast<double>(mem.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
        bool ok = freeGB > 2.0;
        c.passed = ok;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.1f", freeGB);
        c.detail = ok ? "Программа запросила память — свободно " + std::string(buf) + " ГБ."
                      : "Свободно всего " + std::string(buf) + " ГБ.";
        c.explanation = "Мало свободной памяти — игра будет тормозить или не запустится. Программа читает объём свободной памяти через Windows API.";
        c.recommendation = ok ? "" : "Закройте фоновые программы или перезагрузите компьютер.";
    } else {
        c.passed = true;
        c.detail = "Не удалось прочитать память.";
        c.explanation = "";
        c.recommendation = "";
    }
#else
    c.passed = true;
    c.detail = "Не проверяется на этой платформе.";
    c.explanation = "";
    c.recommendation = "";
#endif
    return c;
}

DiagnosticCheck LaunchDiagnostics::checkConflictingProcesses() {
    DiagnosticCheck c;
    c.category = "Система";
    c.name = "Программы, мешающие запуску";
    c.severity = 1;
#ifdef PLATFORM_WINDOWS
    std::vector<std::string> known = {
        "overwolf.exe", "msi_afterburner.exe", "rivatuner.exe", "bandicam.exe",
        "fraps.exe", "dxgi.dll" // dxgi.dll injection (graphics hooks)
    };
    std::vector<std::string> found;
    for (const auto& p : known)
        if (isProcessRunning(p))
            found.push_back(p);
    c.passed = found.empty();
    c.detail = found.empty()
        ? "Программа проверила список процессов-перехватчиков (Overwolf, Afterburner, Bandicam и др.) — ни одного не найдено."
        : "Программа обнаружила в списке процессов: " + [&]() {
            std::string s;
            for (const auto& f : found) { if (!s.empty()) s += ", "; s += f; }
            return s;
        }() + ".";
    c.explanation = "Overlay-программы (Overwolf, RivaTuner, Afterburner) внедряются в игровой процесс и могут вызывать вылеты. Программа просматривает список процессов Windows через Toolhelp API.";
    c.recommendation = found.empty() ? "" : "Закройте эти программы перед запуском игры или добавьте игру в исключения.";
#else
    c.passed = true;
    c.detail = "Не проверяется на этой платформе.";
    c.explanation = "";
    c.recommendation = "";
#endif
    return c;
}

DiagnosticCheck LaunchDiagnostics::checkGameProcess(const std::string& process_name) {
    DiagnosticCheck c;
    c.category = "Игра";
    c.name = "Процесс игры";
    c.severity = 0;
    if (process_name.empty()) {
        c.passed = true;
        c.detail = "Игра ещё не запущена — программа будет следить за процессом.";
        c.explanation = "Программа следит за списком процессов и ждёт появления процесса игры.";
        c.recommendation = "";
        return c;
    }
#ifdef PLATFORM_WINDOWS
    bool running = isProcessRunning(process_name);
    c.passed = running;
    c.detail = running
        ? "Программа видит процесс «" + process_name + "» в списке запущенных процессов."
        : "Процесс «" + process_name + "» не запущен — игра не работает.";
    c.explanation = "Программа ищет исполняемый файл игры в списке процессов Windows. Если процесса нет — игра не запустилась или сразу вылетела.";
    c.recommendation = running ? "" : "Попробуйте запустить игру как администратор, проверьте антивирус (не блокирует ли он exe) и установите обновления.";
#else
    c.passed = true;
    c.detail = "Не проверяется на этой платформе.";
    c.explanation = "";
    c.recommendation = "";
#endif
    return c;
}

DiagnosticCheck LaunchDiagnostics::checkRuntimeLibraries() {
    DiagnosticCheck c;
    c.category = "Система";
    c.name = "Библиотеки времени выполнения (VC++ Redist)";
    c.severity = 2;
#ifdef PLATFORM_WINDOWS
    // look for VC++ redist entries in the uninstall registry
    bool found2015 = false, found2013 = false, found2010 = false;
    const char* keys[] = {
        "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"
    };
    for (const char* rootKey : keys) {
        HKEY hkey;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, rootKey, 0, KEY_READ, &hkey) != ERROR_SUCCESS)
            continue;
        for (DWORD i = 0;; ++i) {
            char nameBuf[256];
            DWORD nameSize = sizeof(nameBuf);
            if (RegEnumKeyExA(hkey, i, nameBuf, &nameSize, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
                break;
            std::string n = nameBuf;
            if (n.find("Visual C++ 2015") != std::string::npos ||
                n.find("Visual C++ 2017") != std::string::npos ||
                n.find("Visual C++ 2019") != std::string::npos ||
                n.find("Visual C++ 2022") != std::string::npos)
                found2015 = true;
            if (n.find("Visual C++ 2013") != std::string::npos) found2013 = true;
            if (n.find("Visual C++ 2010") != std::string::npos) found2010 = true;
        }
        RegCloseKey(hkey);
    }
    bool ok = found2015; // modern games need 2015-2022 runtime
    c.passed = ok;
    c.detail = ok
        ? "Программа нашла в реестре установленный Visual C++ Redistributable 2015+."
        : "Программа не нашла Visual C++ 2015+ в реестре.";
    c.explanation = "Многие игры падают при запуске с ошибкой «VCRUNTIME140.dll not found». Программа ищет записи Visual C++ Redistributable в разделе «Удаление программ» реестра.";
    c.recommendation = ok ? ""
        : "Установите Visual C++ Redistributable (ссылка в разделе «Помощь» программы). Это самая частая причина вылета игр при запуске.";
#else
    c.passed = true;
    c.detail = "Не проверяется на этой платформе.";
    c.explanation = "";
    c.recommendation = "";
#endif
    return c;
}

std::string LaunchDiagnostics::applyFix(const std::string& action_id) {
    // v1.5: route allowlisted fixes through the transactional engine (backup + verify + rollback).
    {
        static gno::remediation::JsonBackupStore backup_store([] {
            const char* app = std::getenv("APPDATA");
            return (std::filesystem::path(app ? app : ".") / "GNO" / "Backups").string();
        }());
        gno::remediation::WindowsStateApi state_api;
        std::string safe_result = gno::remediation::applySafeFix(action_id, state_api, backup_store);
        if (!safe_result.empty())
            return safe_result;
    }
#ifdef PLATFORM_WINDOWS
    if (action_id == "power_plan") {
        std::string out = runCmd("powercfg /setactive 8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c");
        (void)out;
        return "План питания переключён на «Высокая производительность» (powercfg).";
    }
    if (action_id == "game_dvr") {
        std::string out = runCmd("reg add \"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR\" /v AppCaptureEnabled /t REG_DWORD /d 0 /f");
        (void)out;
        return "Запись игр отключена (значение AppCaptureEnabled = 0).";
    }
    if (action_id == "fullscreen_opt") {
        std::string out = runCmd("reg add \"HKCU\\System\\GameConfigStore\" /v GameDVR_FSEBehaviorMode /t REG_DWORD /d 1 /f");
        (void)out;
        return "Оптимизации полноэкранного режима отключены.";
    }
    if (action_id == "tcp") {
        NetworkUtils::applyTCPOptimizations(true);
        return "TCP-стек оптимизирован (адаптивные ACK, NoDelay) в реестре.";
    }
    if (action_id == "vm") {
        std::string out = runCmd("reg add \"HKLM\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management\" /v LargeSystemCache /t REG_DWORD /d 0 /f");
        (void)out;
        return "Оптимизация виртуальной памяти применена (LargeSystemCache = 0).";
    }
    if (action_id == "priority") {
        return "Высокий приоритет будет применён автоматически при запуске игры (профиль игры).";
    }
    if (action_id == "dns") {
        std::string iface = NetworkUtils::getNetworkInterfaceName();
        if (iface != "default") {
            NetworkUtils::setDNS(iface, "1.1.1.1", "1.0.0.1");
            return "DNS изменён на 1.1.1.1 / 1.0.0.1 через netsh.";
        }
        return "Не удалось изменить DNS: сетевой адаптер не найден.";
    }
    if (action_id == "mtu") {
        std::string iface = NetworkUtils::getNetworkInterfaceName();
        if (iface != "default") {
            NetworkUtils::setMTU(iface, 1400);
            return "MTU установлен в 1400 через netsh.";
        }
        return "Не удалось изменить MTU: сетевой адаптер не найден.";
    }
#endif
    return "Неизвестное действие: " + action_id;
}

} // namespace gno