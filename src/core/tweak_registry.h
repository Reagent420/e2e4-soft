#pragma once

// Declarative registry-tweak catalogue (v2.4.0). Each entry = one user-facing
// option that the transactional applier can set/restore automatically.
// Adding new options = adding one line here. No engine changes required.

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace gno {

enum class TweakRoot : int { HKCU = 0, HKLM = 1 };
enum class TweakType : int { Dword = 0, Sz = 1 };

struct TweakSpec {
    const char* id;
    const char* category;
    const char* title;
    const char* description;
    TweakRoot root;
    const char* subkey;
    const char* value_name;
    TweakType type;
    std::uint32_t dword_value;
    const char* sz_value;
    bool needs_reboot;
};

// Curated, widely documented tweaks only. Conservative recommendations.
inline const std::vector<TweakSpec>& tweaks() {
    static const std::vector<TweakSpec> v = {

        // ---------------- СЕТЬ ----------------
        {"net_throttle_off", "Сеть", "Отключить сетевой троттлинг",
         "MMCSS перестаёт ограничивать пропускную способность при воспроизведении медиа",
         TweakRoot::HKLM,
         "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile",
         "NetworkThrottlingIndex", TweakType::Dword, 0xFFFFFFFFu, "", false},

        {"net_games_gpu_priority", "Сеть", "GPU-приоритет для задач мультимедиа",
         "Игровые задачи получают повышенный GPU-приоритет планировщика",
         TweakRoot::HKLM,
         "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games",
         "GPU Priority", TweakType::Dword, 8, "", false},

        {"net_games_priority", "Сеть", "Приоритет CPU для игровых задач",
         "Приоритет 6 в MMCSS для класса Games",
         TweakRoot::HKLM,
         "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games",
         "Priority", TweakType::Dword, 6, "", false},

        {"net_tcp_autotuning", "Сеть", "TCP Autotuning: normal",
         "Гарантирует штатный уровень автонастройки окна TCP",
         TweakRoot::HKLM, "SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters",
         "TcpAutotuning", TweakType::Dword, 1, "", false},

        {"net_nagle_globally", "Сеть", "Отключить Nagle (TcpAckFrequency=1)",
         "Меньше задержек мелких пакетов; применять осознанно",
         TweakRoot::HKLM, "SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters",
         "TcpAckFrequency", TweakType::Dword, 1, "", false},

        // ---------------- ИГРЫ ----------------
        {"game_win32_priority", "Игры", "Win32PrioritySeparation = 38",
         "Процессорное время смещается в пользу активного приложения (игры)",
         TweakRoot::HKLM, "SYSTEM\\CurrentControlSet\\Control\\PriorityControl",
         "Win32PrioritySeparation", TweakType::Dword, 38, "", true},

        {"game_hags_on", "Игры", "HAGS: аппаратное планирование GPU",
         "Windows Hardware-Accelerated GPU Scheduling включён",
         TweakRoot::HKLM, "SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers",
         "HwSchMode", TweakType::Dword, 2, "", true},

        {"game_gamedvr_store", "Игры", "Game DVR в GameConfigStore выключен",
         "Дублирующее отключение записи клипов (вместе с действием Game DVR)",
         TweakRoot::HKCU, "System\\GameConfigStore",
         "GameDVR_Enabled", TweakType::Dword, 0, "", false},

        // ---------------- МЫШЬ / ВВОД ----------------
        {"mouse_menudelay_zero", "Мышь и ввод", "Задержка появления меню = 0 мс",
         "Мгновенное открытие меню интерфейса",
         TweakRoot::HKCU, "Control Panel\\Desktop",
         "MenuShowDelay", TweakType::Sz, 0, "0", false},

        // ---------------- ЭФФЕКТЫ ----------------
        {"fx_minanimate", "Эффекты", "Анимация сворачивания окон выключена",
         "Убирает анимацию minimize/restore",
         TweakRoot::HKCU, "Control Panel\\Desktop\\WindowMetrics",
         "MinAnimate", TweakType::Sz, 0, "0", false},

        {"fx_taskbar_anim", "Эффекты", "Анимация панели задач выключена",
         "Мгновенные всплывания панели задач",
         TweakRoot::HKCU, "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
         "TaskbarAnimations", TweakType::Dword, 0, "", false},

        {"fx_listview_alpha", "Эффекты", "Полупрозрачное выделение выключено",
         "Классическое сплошное выделение в списках",
         TweakRoot::HKCU, "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
         "ListviewAlphaSelect", TweakType::Dword, 0, "", false},

        {"fx_listview_shadow", "Эффекты", "Тени под элементами выключены",
         "Меньше композитных эффектов при отрисовке",
         TweakRoot::HKCU, "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
         "ListviewShadow", TweakType::Dword, 0, "", false},

        // ---------------- КОНФИДЕНЦИАЛЬНОСТЬ ----------------
        {"prv_advertising_id", "Приватность", "Рекламный идентификатор выключен",
         "Windows не выдаёт рекламный ID приложениям",
         TweakRoot::HKCU, "Software\\Microsoft\\Windows\\CurrentVersion\\AdvertisingInfo",
         "Enabled", TweakType::Dword, 0, "", false},

        {"prv_ink_personalization", "Приватность", "Персонализация ввода ограничена",
         "Запрет автоматического сбора рукописного ввода",
         TweakRoot::HKCU, "Software\\Microsoft\\InputPersonalization",
         "RestrictImplicitInkCollection", TweakType::Dword, 1, "", false},

        {"prv_steps_recorder", "Приватность", "Steps Recorder отключён",
         "Утилита записи шагов пользователя не работает",
         TweakRoot::HKLM, "SOFTWARE\\Policies\\Microsoft\\Windows\\AppCompat",
         "DisableUAR", TweakType::Dword, 1, "", false},

        {"prv_activity_upload", "Приватность", "Выгрузка журнала действий выключена",
         "Timeline не отправляет активность в облако",
         TweakRoot::HKLM, "SOFTWARE\\Policies\\Microsoft\\Windows\\System",
         "UploadUserActivities", TweakType::Dword, 0, "", false},

        // ---------------- ПИТАНИЕ ----------------
        {"pow_usb_selective", "Питание", "USB Selective Suspend выключен",
         "USB-устройства не уходят в экономию (стабильнее для мышей/клавиатур)",
         TweakRoot::HKLM, "SYSTEM\\CurrentControlSet\\Services\\USB",
         "DisableSelectiveSuspend", TweakType::Dword, 1, "", true},

        {"pow_throttling_off", "Питание", "Power Throttling выключен",
         "Ядра не переводят фоновые процессы в энергосберегающий режим",
         TweakRoot::HKLM, "SYSTEM\\CurrentControlSet\\Control\\Power\\PowerThrottling",
         "PowerThrottlingOff", TweakType::Dword, 1, "", false},

        // ---------------- ПРОВОДНИК / QoL ----------------
        {"exp_startup_delay", "Проводник", "Задержка старта приложений = 0",
         "Убирает искусственную паузу при запуске программ после входа",
         TweakRoot::HKCU, "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Serialize",
         "StartupDelayInMSec", TweakType::Dword, 0, "", false},

        {"exp_bing_suggestions", "Проводник", "Bing-подсказки в поиске Пуска выключены",
         "Локальный поиск без веб-вставок",
         TweakRoot::HKCU, "Software\\Policies\\Microsoft\\Windows\\Explorer",
         "DisableSearchBoxSuggestions", TweakType::Dword, 1, "", false},

        {"exp_widgets_off", "Проводник", "Кнопка Widgets убрана", 
         "Виджеты Windows не загружаются в панель задач",
         TweakRoot::HKCU, "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
         "TaskbarDa", TweakType::Dword, 0, "", false},

        {"exp_chat_off", "Проводник", "Кнопка Chat убрана",
         "Teams Chat не резервирует место и память",
         TweakRoot::HKCU, "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
         "TaskbarCc", TweakType::Dword, 0, "", false},
        // ---------------- FPS BOOST (v2.4.0) ----------------
        {"fps_system_resp", "FPS Boost", "System Responsiveness = 0",
         "Все ядра CPU отдаются игре вместо фоновых задач (по умолчанию 20%)",
         TweakRoot::HKLM,
         "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile",
         "SystemResponsiveness", TweakType::Dword, 0, "", false},

        {"fps_bg_apps_off", "FPS Boost", "Фоновые приложения выключены",
         "UWP-приложения не работают в фоне во время игры",
         TweakRoot::HKCU,
         "Software\\Microsoft\\Windows\\CurrentVersion\\BackgroundAccessApplications",
         "GlobalUserDisabled", TweakType::Dword, 1, "", false},

        {"fx_visual_perf", "Эффекты", "Пресет: максимальная производительность",
         "Windows Visual Effects = Best Performance (все анимации и эффекты выключены)",
         TweakRoot::HKCU, "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VisualEffects",
         "VisualFXSetting", TweakType::Dword, 2, "", true},

        {"game_delivery_opt", "Игры", "Delivery Optimization выключен",
         "Windows Update не раздаёт обновления через P2P другим компьютерам",
         TweakRoot::HKLM, "SYSTEM\\CurrentControlSet\\Services\\DoSvc",
         "Start", TweakType::Dword, 4, "", false},
    };
    return v;
}

// ---------------------------------------------------------------- External loading

/// Parses one .tweak file (INI format). Returns nullopt on parse error.
inline std::optional<TweakSpec> parseTweakFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) return std::nullopt;

    TweakSpec s{};
    bool has_id = false;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);
        auto b = [&]() { return val == "1" || val == "true"; };
        if (key == "id") { s.id = _strdup(val.c_str()); has_id = true; }
        else if (key == "category") s.category = _strdup(val.c_str());
        else if (key == "title") s.title = _strdup(val.c_str());
        else if (key == "description") s.description = _strdup(val.c_str());
        else if (key == "root") s.root = (val == "HKLM") ? TweakRoot::HKLM : TweakRoot::HKCU;
        else if (key == "subkey") s.subkey = _strdup(val.c_str());
        else if (key == "value_name") s.value_name = _strdup(val.c_str());
        else if (key == "type") s.type = (val == "sz") ? TweakType::Sz : TweakType::Dword;
        else if (key == "dword_value") { try { s.dword_value = static_cast<std::uint32_t>(std::stoul(val)); } catch (...) {} }
        else if (key == "sz_value") s.sz_value = _strdup(val.c_str());
        else if (key == "needs_reboot") s.needs_reboot = b();
    }
    if (!has_id) return std::nullopt;
    return s;
}

/// Loads all .tweak files from a directory into the external tweaks vector.
inline void loadExternalTweaks(const std::string& dir_path,
                                std::vector<TweakSpec>& external) {
    std::error_code ec;
    if (!std::filesystem::exists(dir_path, ec)) return;
    for (const auto& entry : std::filesystem::directory_iterator(dir_path, ec)) {
        if (entry.path().extension() != ".tweak") continue;
        if (auto spec = parseTweakFile(entry.path().string()))
            external.push_back(std::move(*spec));
    }
}

/// Returns builtin + external tweaks combined.
inline std::vector<TweakSpec> allTweaks(const std::string& external_dir = "") {
    auto result = tweaks();
    if (!external_dir.empty()) {
        std::vector<TweakSpec> ext;
        loadExternalTweaks(external_dir, ext);
        result.insert(result.end(), ext.begin(), ext.end());
    }
    return result;
}

} // namespace gno
