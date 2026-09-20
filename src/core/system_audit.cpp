#include "system_audit.h"

#include <sstream>
#include <fstream>

#include "remediation/boost_gate.h"
#include "remediation/remediation_service.h"
#include "remediation/target_discovery.h"
#include "remediation/windows_state_api.h"

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#include <winreg.h>
#endif

namespace gno {

namespace {

namespace rem = remediation;

// Единственный способ прочитать состояние — спросить сам движок. Дублировать
// пути реестра нельзя: они уже один раз разошлись с движком, и вкладка
// «Что изменилось» показывала «не применилось» там, где всё применилось.
bool readAllowedRegistry(rem::AllowedRegistryKey key, std::uint32_t& out) {
    rem::WindowsStateApi api;
    auto data = api.getAllowedRegistry(key);
    if (!data || !data.value().existed) return false;
    out = data.value().value;
    return true;
}

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
    std::uint32_t v = 0;
    if (!readAllowedRegistry(rem::AllowedRegistryKey::GameDvrEnabled, v)) return "";
    return std::to_string(v);
}

// Движок отключает оптимизации полноэкранного режима не через
// GameDVR_FSEBehaviorMode (этот ключ он вообще не трогает), а через флаги
// совместимости конкретного exe. Поэтому и читаем именно их.
std::string SystemAudit::readFullscreenOptValue() {
    auto game = rem::discoverRunningGameProcess();
    if (!game) return ""; // игра не запущена — проверять нечего

    rem::WindowsStateApi api;
    auto state = api.getFullscreenOptimizations(rem::ExecutableTarget{game->path});
    if (!state) return "";
    const auto& v = state.value();
    // Ищем ровно тот флаг, который ставит движок, а не любое совпадение
    // подстроки — иначе чужой флаг в той же строке дал бы ложное «отключено».
    if (v.existed && v.compatibility_flags.find(rem::kEngineFullscreenFlags) != std::string::npos)
        return kAuditOffText;
    // Записи нет или флаг не выставлен — оптимизации работают.
    return kAuditOnText;
}

std::string SystemAudit::readGameModeValue() {
    std::uint32_t v = 0;
    if (!readAllowedRegistry(rem::AllowedRegistryKey::GameModeAllow, v)) return "";
    return std::to_string(v);
}

std::string SystemAudit::readTcpRttValue() {
    std::uint32_t v = 0;
    if (!readAllowedRegistry(rem::AllowedRegistryKey::TcpInitialRetransmissionTimeout, v)) return "";
    return std::to_string(v);
}

std::string SystemAudit::readActivePowerPlan() {
    rem::WindowsStateApi api;
    auto plan = api.getPowerPlan();
    if (!plan) return "";
    return plan.value().identifier;
}

std::string SystemAudit::readMouseAccelValue() {
    rem::WindowsStateApi api;
    auto ma = api.getMouseAccel();
    if (!ma) return "";
    const auto& v = ma.value();
    const bool off = v.speed == 0 && v.threshold1 == 0 && v.threshold2 == 0;
    return off ? kAuditOffText : kAuditOnText;
}

std::string SystemAudit::readDnsServersValue() {
    auto iface = rem::discoverPrimaryInterface();
    if (!iface) return "";
    const auto* target = std::get_if<rem::InterfaceTarget>(&*iface);
    if (!target) return "";
    rem::WindowsStateApi api;
    auto dns = api.getDns(*target);
    if (!dns) return "";
    return rem::summarizeValue(rem::ActionValue{dns.value()});
}

std::string SystemAudit::readMtuValue() {
    auto iface = rem::discoverPrimaryInterface();
    if (!iface) return "";
    const auto* target = std::get_if<rem::InterfaceTarget>(&*iface);
    if (!target) return "";
    rem::WindowsStateApi api;
    auto mtu = api.getMtu(*target);
    if (!mtu) return "";
    return rem::summarizeValue(rem::ActionValue{mtu.value()});
}

std::vector<SettingChange> SystemAudit::verifyFpsSettings() {
    std::vector<SettingChange> out;

    SettingChange dvr;
    dvr.section = "FPS";
    dvr.name = "Запись игр (GameDVR_Enabled)";
    dvr.action = "Отключить фоновую запись";
    dvr.old_value = readGameDvrValue();
    dvr.new_value = "0 (выключено)";
    if (dvr.old_value == "0") {
        dvr.status = SettingChange::Status::Verified;
        dvr.detail = "Запись игр отключена — движок видит GameDVR_Enabled = 0.";
    } else if (dvr.old_value.empty()) {
        dvr.status = SettingChange::Status::NotApplied;
        dvr.detail = "Параметр не найден в реестре — оптимизация не применялась.";
    } else {
        dvr.status = SettingChange::Status::Failed;
        dvr.detail = "Запись игр всё ещё включена (GameDVR_Enabled = " + dvr.old_value + ").";
    }
    out.push_back(dvr);

    SettingChange fse;
    fse.section = "FPS";
    fse.name = "Оптимизации полноэкранного режима (флаг совместимости)";
    fse.action = "Отключить задержку ввода";
    fse.old_value = readFullscreenOptValue();
    fse.new_value = kAuditOffText;
    if (fse.old_value == kAuditOffText) {
        fse.status = SettingChange::Status::Verified;
        fse.detail = "Для запущенной игры выставлен флаг совместимости — оптимизации отключены.";
    } else if (fse.old_value.empty()) {
        fse.status = SettingChange::Status::NotApplied;
        fse.detail = "Игра не запущена — проверить можно только при запущенной игре.";
    } else {
        fse.status = SettingChange::Status::NotApplied;
        fse.detail = "Флаг совместимости не выставлен — оптимизации полноэкранного режима работают.";
    }
    out.push_back(fse);

    SettingChange gm;
    gm.section = "FPS";
    gm.name = "Игровой режим (AllowAutoGameMode)";
    gm.action = "Отключить Game Mode";
    gm.old_value = readGameModeValue();
    gm.new_value = "0 (выключен)";
    if (gm.old_value == "0") {
        gm.status = SettingChange::Status::Verified;
        gm.detail = "Игровой режим отключён — движок видит AllowAutoGameMode = 0.";
    } else if (gm.old_value.empty()) {
        gm.status = SettingChange::Status::NotApplied;
        gm.detail = "Параметр не найден — оптимизация не применялась.";
    } else {
        gm.status = SettingChange::Status::Failed;
        gm.detail = "Игровой режим включён (AllowAutoGameMode = " + gm.old_value + ").";
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
        pp.detail = "Активен план «Высокая производительность» — Windows подтверждает по API.";
    } else if (pp.old_value.empty()) {
        pp.status = SettingChange::Status::NotApplied;
        pp.detail = "Не удалось прочитать активный план питания.";
    } else {
        pp.status = SettingChange::Status::NotApplied;
        pp.detail = "Активен другой план питания (" + pp.old_value + ").";
    }
    out.push_back(pp);

    return out;
}

std::vector<SettingChange> SystemAudit::verifyNetworkSettings() {
    std::vector<SettingChange> out;

    // Движок настраивает именно InitialRtt (HKLM\...\Tcpip\Parameters). Раньше
    // здесь проверялись TcpAckFrequency/TCPNoDelay из Internet Settings —
    // параметры, которые движок никогда не пишет, поэтому проверка всегда
    // показывала «не применено» после успешного применения.
    SettingChange rtt;
    rtt.section = "Network";
    rtt.name = "Начальный RTT TCP (TcpInitialRtt)";
    rtt.action = "Ускорить установку соединения";
    rtt.old_value = readTcpRttValue();
    rtt.new_value = std::to_string(rem::kEngineTcpInitialRtt);
    if (rtt.old_value == rtt.new_value) {
        rtt.status = SettingChange::Status::Verified;
        rtt.detail = "TcpInitialRtt = " + rtt.old_value + " — оптимизация TCP активна.";
    } else if (rtt.old_value.empty()) {
        rtt.status = SettingChange::Status::NotApplied;
        rtt.detail = "Параметр не задан — TCP-оптимизация не применялась.";
    } else {
        rtt.status = SettingChange::Status::NotApplied;
        rtt.detail = "TcpInitialRtt = " + rtt.old_value + " — движок это значение не менял.";
    }
    out.push_back(rtt);

    const std::string dns_expected =
        std::string(rem::kEngineDnsPrimary) + ", " + rem::kEngineDnsSecondary;
    SettingChange dns;
    dns.section = "Network";
    dns.name = "DNS-серверы";
    dns.action = "Быстрые резолверы";
    dns.old_value = readDnsServersValue();
    dns.new_value = dns_expected;
    if (dns.old_value == dns_expected) {
        dns.status = SettingChange::Status::Verified;
        dns.detail = "Активный адаптер использует " + dns.old_value + ".";
    } else if (dns.old_value.empty()) {
        dns.status = SettingChange::Status::NotApplied;
        dns.detail = "Не удалось прочитать DNS активного адаптера.";
    } else {
        dns.status = SettingChange::Status::NotApplied;
        dns.detail = "Активный адаптер использует " + dns.old_value +
                     " — программа DNS не меняла.";
    }
    out.push_back(dns);

    SettingChange mtu;
    mtu.section = "Network";
    mtu.name = "MTU активного адаптера";
    mtu.action = "Убрать фрагментацию";
    mtu.old_value = readMtuValue();
    mtu.new_value = std::to_string(rem::kEngineMtuValue) + " байт";
    if (mtu.old_value == mtu.new_value) {
        mtu.status = SettingChange::Status::Verified;
        mtu.detail = "MTU = " + mtu.old_value + " — как задал движок.";
    } else if (mtu.old_value.empty()) {
        mtu.status = SettingChange::Status::NotApplied;
        mtu.detail = "Не удалось прочитать MTU активного адаптера.";
    } else {
        mtu.status = SettingChange::Status::NotApplied;
        mtu.detail = "MTU = " + mtu.old_value + " — значение движка не применялось.";
    }
    out.push_back(mtu);

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
        "Программа читает GameDVR_Enabled через движок. Сейчас он = " + (readGameDvrValue().empty() ? "не задан" : readGameDvrValue()) + ".",
        false, false, "Доступно (работает для текущего пользователя)");

    add("fullscreen_opt", "Отключение оптимизаций полноэкранного режима",
        "Убирает задержку ввода в полноэкранных играх.",
        "Программа ставит флаг совместимости для exe запущенной игры. Сейчас = " +
            (readFullscreenOptValue().empty() ? "игра не запущена" : readFullscreenOptValue()) + ".",
        false, false, "Доступно (нужна запущенная игра)");

    add("mouse_accel", "Отключение ускорения мыши",
        "Прямой ввод от мыши — точнее прицеливание в шутерах.",
        "Программа читает Control Panel\\Mouse и может изменить его. Сейчас = " +
            (readMouseAccelValue().empty() ? "не прочитано" : readMouseAccelValue()) + ".",
        false, false, "Доступно");

    add("power_plan", "План «Высокая производительность»",
        "Переводит Windows на план питания с максимальной производительностью.",
        "Программа спрашивает активный план у Windows. Сейчас = " + (readActivePowerPlan().empty() ? "не определён" : readActivePowerPlan()) + ".",
        true, false, admin ? "Доступно (права есть)" : "Требуются права администратора");

    add("priority", "Высокий приоритет игрового процесса",
        "Игра получает больше ресурсов CPU, чем фоновые программы.",
        "Программа следит за процессом игры и поднимает его приоритет через Windows API.",
        false, false, "Доступно (применяется при запуске игры)");

    add("virtual_memory", "Оптимизация виртуальной памяти",
        "Настраивает системный кэш для уменьшения подтормаживаний.",
        "Программа этого не делает: параметр вне белого списка движка, "
        "изменение системного кэша небезопасно. Настраивается вручную: "
        "Свойства системы → Дополнительно → Быстродействие.",
        true, false, "Недоступно (вне белого списка движка)");

    add("tcp_opt", "Оптимизация TCP-стека",
        "Начальный RTT TCP снижает время установки игровых соединений.",
        "Программа записывает TcpInitialRtt в HKLM\\SYSTEM\\CurrentControlSet\\Services\\"
        "Tcpip\\Parameters. Сейчас = " + (readTcpRttValue().empty() ? "не задано" : readTcpRttValue()) + ".",
        true, false, admin ? "Доступно (права есть)" : "Требуются права администратора");

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