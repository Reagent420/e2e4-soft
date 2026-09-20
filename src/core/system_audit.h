#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace gno {

// One recorded setting change with before/after values and verification state.
struct SettingChange {
    std::string section;      // "FPS", "Network", "System"
    std::string name;         // human readable name
    std::string action;       // short action description
    std::string old_value;    // value before applying
    std::string new_value;    // value after applying
    enum class Status { Applied, AdminRequired, Failed, Verified, NotApplied } status = Status::NotApplied;
    std::string detail;       // explanation, e.g. why admin rights are needed
};

// A capability describes something the program can or cannot do right now.
struct Capability {
    std::string id;
    std::string title;
    std::string description;   // what it does
    std::string what_it_sees;  // how the program detects it ("как программа это видит")
    bool requires_admin = false;
    bool currently_possible = false; // can be applied right now
    std::string status_text;   // e.g. "Доступно" / "Требуются права администратора" / "Недоступно"
    bool requires_vpn_server = false;
};

struct AuditSnapshot {
    std::vector<SettingChange> changes;
};

// Канонические человекочитаемые значения («отключено»/«включено»). UI сравнивает
// с ними прочитанное из системы, поэтому обе стороны обязаны брать строки
// отсюда, а не дублировать литералы: расхождение молча превращает строку
// «Что изменилось» в вечное «не применилось».
inline constexpr const char* kAuditOffText = "отключено";
inline constexpr const char* kAuditOnText = "включено";

class SystemAudit {
public:
    // Elevation / permission checks
    static bool isAdmin();
    static bool canWriteToFile(const std::string& path);

    // Read-back verification of previously applied optimizations.
    // Returns a list of SettingChange records that verify current state.
    static std::vector<SettingChange> verifyFpsSettings();
    static std::vector<SettingChange> verifyNetworkSettings();

    // Capture the current state of a setting before changing it.
    //
    // ВАЖНО: все значения читаются через remediation::WindowsStateApi — тот же
    // API, которым движок их пишет. Раньше здесь были продублированы пути
    // реестра, и они разошлись с движком: проверка смотрела в другие ветки и
    // объявляла «не применилось» то, что на самом деле применилось. Пока чтение
    // идёт через движок, разойтись снова оно не может.
    //
    // Возвращают человекочитаемое значение, а не сырой мусор из реестра.
    static std::string readGameDvrValue();       // GameDVR_Enabled: 0/1
    static std::string readFullscreenOptValue(); // «отключено»/«включено»/«не задано»
    static std::string readGameModeValue();      // AllowAutoGameMode: 0/1
    static std::string readTcpRttValue();        // TcpInitialRtt, напр. «5000»
    static std::string readActivePowerPlan();    // GUID активной схемы, в нижнем регистре
    static std::string readMouseAccelValue();    // «отключено»/«включено»
    static std::string readDnsServersValue();    // «1.1.1.1, 1.0.0.1» или «авто (провайдер)»
    static std::string readMtuValue();           // напр. «1500 байт»

    // Capability matrix: what the program can and cannot do in the current session.
    static std::vector<Capability> getCapabilities();

    // Formats a list of changes as a readable multi-line string (used by console/tests).
    static std::string formatChanges(const std::vector<SettingChange>& changes);
};

} // namespace gno