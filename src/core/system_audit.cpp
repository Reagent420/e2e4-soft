#include "system_audit.h"

#include <sstream>
#include <fstream>

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#include <winreg.h>
#endif

namespace gno {

namespace {

#ifdef PLATFORM_WINDOWS
std::string readRegDword(HKEY root, const char* subkey, const char* name) {
    HKEY hkey;
    if (RegOpenKeyExA(root, subkey, 0, KEY_READ, &hkey) != ERROR_SUCCESS)
        return "";
    DWORD value = 0;
    DWORD size = sizeof(value);
    DWORD type = 0;
    LONG res = RegQueryValueExA(hkey, name, nullptr, &type, reinterpret_cast<BYTE*>(&value), &size);
    RegCloseKey(hkey);
    if (res != ERROR_SUCCESS || type != REG_DWORD)
        return "";
    return std::to_string(value);
}

bool writeRegDword(HKEY root, const char* subkey, const char* name, DWORD value) {
    HKEY hkey;
    if (RegOpenKeyExA(root, subkey, 0, KEY_SET_VALUE, &hkey) != ERROR_SUCCESS)
        return false;
    LONG res = RegSetValueExA(hkey, name, 0, REG_DWORD, reinterpret_cast<BYTE*>(&value), sizeof(value));
    RegCloseKey(hkey);
    return res == ERROR_SUCCESS;
}
#endif

} // namespace

bool SystemAudit::isAdmin() {
#ifdef PLATFORM_WINDOWS
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    TOKEN_ELEVATION elev;
    DWORD size = sizeof(elev);
    BOOL ok = GetTokenInformation(token, TokenElevation, &elev, size, &size);
    CloseHandle(token);
    return ok && elev.TokenIsElevated;
#else
    return false;
#endif
}

bool SystemAudit::canWriteToFile(const std::string& path) {
    std::ofstream probe(path, std::ios::app);
    if (!probe.is_open())
        return false;
    probe.close();
    return true;
}

std::string SystemAudit::readGameDvrValue() {
#ifdef PLATFORM_WINDOWS
    return readRegDword(HKEY_CURRENT_USER,
                        "Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR",
                        "AppCaptureEnabled");
#else
    return "";
#endif
}

std::string SystemAudit::readFullscreenOptValue() {
#ifdef PLATFORM_WINDOWS
    return readRegDword(HKEY_CURRENT_USER, "System\\GameConfigStore",
                        "GameDVR_FSEBehaviorMode");
#else
    return "";
#endif
}

std::string SystemAudit::readGameModeValue() {
#ifdef PLATFORM_WINDOWS
    return readRegDword(HKEY_CURRENT_USER,
                        "Software\\Microsoft\\Windows\\CurrentVersion\\GameBar",
                        "AutoGameModeEnabled");
#else
    return "";
#endif
}

std::string SystemAudit::readTcpValue(const char* value_name) {
#ifdef PLATFORM_WINDOWS
    return readRegDword(HKEY_CURRENT_USER,
                        "Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings",
                        value_name);
#else
    (void)value_name;
    return "";
#endif
}

std::string SystemAudit::readActivePowerPlan() {
#ifdef PLATFORM_WINDOWS
    // registry mirror of the active power scheme
    HKEY hkey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "SYSTEM\\CurrentControlSet\\Control\\Power\\User\\PowerSchemes",
                      0, KEY_READ, &hkey) != ERROR_SUCCESS)
        return "";
    char buf[128] = {0};
    DWORD size = sizeof(buf);
    LONG res = RegQueryValueExA(hkey, "ActivePowerScheme", nullptr, nullptr,
                                reinterpret_cast<BYTE*>(buf), &size);
    RegCloseKey(hkey);
    if (res != ERROR_SUCCESS || size == 0)
        return "";
    return buf;
#else
    return "";
#endif
}

std::vector<SettingChange> SystemAudit::verifyFpsSettings() {
    std::vector<SettingChange> out;

    SettingChange dvr;
    dvr.section = "FPS";
    dvr.name = "Запись игр (Game DVR)";
    dvr.action = "Отключить фоновую запись";
    dvr.old_value = readGameDvrValue();
    dvr.new_value = "0 (выключено)";
    if (dvr.old_value == "0") {
        dvr.status = SettingChange::Status::Verified;
        dvr.detail = "Запись игр отключена — программа видит значение 0 в реестре.";
    } else if (dvr.old_value.empty()) {
        dvr.status = SettingChange::Status::NotApplied;
        dvr.detail = "Параметр не найден в реестре — оптимизация не применялась.";
    } else {
        dvr.status = SettingChange::Status::Failed;
        dvr.detail = "Запись игр всё ещё включена (значение " + dvr.old_value + "). Нужны права администратора или проверьте настройки Windows.";
    }
    out.push_back(dvr);

    SettingChange fse;
    fse.section = "FPS";
    fse.name = "Оптимизации полноэкранного режима";
    fse.action = "Отключить задержку ввода";
    fse.old_value = readFullscreenOptValue();
    fse.new_value = "1 (отключено)";
    if (fse.old_value == "1") {
        fse.status = SettingChange::Status::Verified;
        fse.detail = "Оптимизации полноэкранного режима отключены — программа видит значение 1.";
    } else if (fse.old_value.empty()) {
        fse.status = SettingChange::Status::NotApplied;
        fse.detail = "Параметр не найден — оптимизация не применялась.";
    } else {
        fse.status = SettingChange::Status::Failed;
        fse.detail = "Параметр всё ещё = " + fse.old_value + ". Не удалось применить.";
    }
    out.push_back(fse);

    SettingChange gm;
    gm.section = "FPS";
    gm.name = "Игровой режим Windows";
    gm.action = "Отключить Game Mode";
    gm.old_value = readGameModeValue();
    gm.new_value = "0 (выключен)";
    if (gm.old_value == "0") {
        gm.status = SettingChange::Status::Verified;
        gm.detail = "Игровой режим отключён — программа видит значение 0.";
    } else if (gm.old_value.empty()) {
        gm.status = SettingChange::Status::NotApplied;
        gm.detail = "Параметр не найден — оптимизация не применялась.";
    } else {
        gm.status = SettingChange::Status::Failed;
        gm.detail = "Игровой режим включён (значение " + gm.old_value + ").";
    }
    out.push_back(gm);

    SettingChange pp;
    pp.section = "FPS";
    pp.name = "План питания";
    pp.action = "Высокая производительность";
    pp.old_value = readActivePowerPlan();
    pp.new_value = "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c (Высокая производительность)";
    if (pp.old_value == "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c") {
        pp.status = SettingChange::Status::Verified;
        pp.detail = "Активен план «Высокая производительность» — программа видит его GUID в реестре.";
    } else if (pp.old_value.empty()) {
        pp.status = SettingChange::Status::NotApplied;
        pp.detail = "Не удалось прочитать активный план питания.";
    } else {
        pp.status = SettingChange::Status::Failed;
        pp.detail = "Активен другой план питания (" + pp.old_value + "). Не применялся или был откатан.";
    }
    out.push_back(pp);

    return out;
}

std::vector<SettingChange> SystemAudit::verifyNetworkSettings() {
    std::vector<SettingChange> out;

    SettingChange ack;
    ack.section = "Network";
    ack.name = "Адаптивные ACK (TcpAckFrequency)";
    ack.action = "Снизить задержку подтверждений";
    ack.old_value = readTcpValue("TcpAckFrequency");
    ack.new_value = "1";
    if (ack.old_value == "1") {
        ack.status = SettingChange::Status::Verified;
        ack.detail = "Программа видит TcpAckFrequency = 1 в реестре — оптимизация активна.";
    } else if (ack.old_value.empty()) {
        ack.status = SettingChange::Status::NotApplied;
        ack.detail = "Параметр не найден — TCP-оптимизация не применялась.";
    } else {
        ack.status = SettingChange::Status::Failed;
        ack.detail = "TcpAckFrequency = " + ack.old_value + " — не применено.";
    }
    out.push_back(ack);

    SettingChange nodelay;
    nodelay.section = "Network";
    nodelay.name = "TCP NoDelay";
    nodelay.action = "Отключить задержку отправки";
    nodelay.old_value = readTcpValue("TCPNoDelay");
    nodelay.new_value = "1";
    if (nodelay.old_value == "1") {
        nodelay.status = SettingChange::Status::Verified;
        nodelay.detail = "Программа видит TCPNoDelay = 1 — оптимизация активна.";
    } else if (nodelay.old_value.empty()) {
        nodelay.status = SettingChange::Status::NotApplied;
        nodelay.detail = "Параметр не найден — TCP-оптимизация не применялась.";
    } else {
        nodelay.status = SettingChange::Status::Failed;
        nodelay.detail = "TCPNoDelay = " + nodelay.old_value + " — не применено.";
    }
    out.push_back(nodelay);

    return out;
}

std::vector<Capability> SystemAudit::getCapabilities() {
    std::vector<Capability> caps;
    bool admin = isAdmin();

    auto add = [&](const std::string& id, const std::string& title,
                   const std::string& desc, const std::string& sees,
                   bool req_admin, bool req_vpn, const std::string& status) {
        Capability c;
        c.id = id;
        c.title = title;
        c.description = desc;
        c.what_it_sees = sees;
        c.requires_admin = req_admin;
        c.requires_vpn_server = req_vpn;
        c.currently_possible = !req_vpn && (!req_admin || admin);
        c.status_text = status;
        caps.push_back(c);
    };

    add("game_dvr", "Отключение записи игр",
        "Останавливает фоновую запись Windows Game DVR, которая снижает FPS.",
        "Программа читает параметр AppCaptureEnabled в реестре. Сейчас он = " + (readGameDvrValue().empty() ? "не задан" : readGameDvrValue()) + ".",
        false, false, "Доступно (работает для текущего пользователя)");

    add("fullscreen_opt", "Отключение оптимизаций полноэкранного режима",
        "Убирает задержку ввода в полноэкранных играх.",
        "Программа проверяет GameDVR_FSEBehaviorMode. Сейчас = " + (readFullscreenOptValue().empty() ? "не задан" : readFullscreenOptValue()) + ".",
        false, false, "Доступно");

    add("mouse_accel", "Отключение ускорения мыши",
        "Прямой ввод от мыши — точнее прицеливание в шутерах.",
        "Программа проверяет параметры мыши в реестре и может их изменить.",
        false, false, "Доступно");

    add("power_plan", "План «Высокая производительность»",
        "Переводит Windows на план питания с максимальной производительностью.",
        "Программа читает ActivePowerScheme. Активный план = " + (readActivePowerPlan().empty() ? "не определён" : readActivePowerPlan()) + ".",
        true, false, admin ? "Доступно (права есть)" : "Требуются права администратора");

    add("priority", "Высокий приоритет игрового процесса",
        "Игра получает больше ресурсов CPU, чем фоновые программы.",
        "Программа следит за процессом игры и поднимает его приоритет через Windows API.",
        false, false, "Доступно (применяется при запуске игры)");

    add("virtual_memory", "Оптимизация виртуальной памяти",
        "Настраивает системный кэш для уменьшения подтормаживаний.",
        "Программа изменяет LargeSystemCache в реестре.",
        true, false, admin ? "Доступно (права есть)" : "Требуются права администратора");

    add("tcp_opt", "Оптимизация TCP-стека",
        "Адаптивные ACK и NoDelay снижают задержку в играх.",
        "Программа записывает TcpAckFrequency и TCPNoDelay в реестр. Сейчас = " + (readTcpValue("TcpAckFrequency").empty() ? "не задано" : readTcpValue("TcpAckFrequency")) + ".",
        false, false, "Доступно (для текущего пользователя)");

    add("mtu", "Оптимизация MTU",
        "Подбор размера пакета уменьшает фрагментацию и потери.",
        "Программа выполняет netsh ipv4 set subinterface mtu. Требует администратора.",
        true, false, admin ? "Доступно (права есть)" : "Требуются права администратора");

    add("dns", "Смена DNS-сервера",
        "Быстрый DNS ускоряет подключение к игровым серверам.",
        "Программа выполняет netsh interface ip set dns. Требует администратора.",
        true, false, admin ? "Доступно (права есть)" : "Требуются права администратора");

    add("multipath", "Мультимаршрутный режим",
        "Данные идут по нескольким путям одновременно — ниже пинг и потери.",
        "Требует собственные серверы в разных точках мира. Без них физически невозможно направить трафик по другому маршруту.",
        false, true, "Будет доступно после подключения серверной сети");

    add("autoselect", "Автовыбор лучшего маршрута",
        "Программа переключает трафик на самый быстрый маршрут в реальном времени.",
        "Анализирует пинг по всем маршрутам и выбирает лучший. Нужна серверная сеть.",
        false, true, "Будет доступно после подключения серверной сети");

    add("loss_comp", "Компенсация потерь пакетов",
        "Отправляет дубликаты пакетов при нестабильном соединении.",
        "Реализуется на серверах. Локально это сделать невозможно.",
        false, true, "Будет доступно после подключения серверной сети");

    return caps;
}

std::string SystemAudit::formatChanges(const std::vector<SettingChange>& changes) {
    std::ostringstream out;
    for (const auto& c : changes) {
        const char* status = "?";
        switch (c.status) {
            case SettingChange::Status::Applied:     status = "APPLIED"; break;
            case SettingChange::Status::AdminRequired: status = "ADMIN NEEDED"; break;
            case SettingChange::Status::Failed:      status = "FAILED"; break;
            case SettingChange::Status::Verified:    status = "VERIFIED"; break;
            case SettingChange::Status::NotApplied:  status = "NOT APPLIED"; break;
        }
        out << "[" << status << "] " << c.section << " :: " << c.name << "\n"
            << "    action : " << c.action << "\n"
            << "    old    : " << (c.old_value.empty() ? "-" : c.old_value) << "\n"
            << "    new    : " << c.new_value << "\n"
            << "    detail : " << c.detail << "\n";
    }
    return out.str();
}

} // namespace gno