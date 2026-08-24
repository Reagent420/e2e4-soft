#pragma once

// Declarative registry-tweak catalogue (v2.4.0). Each entry = one user-facing
// option that the transactional applier can set/restore automatically.
// Adding new options = adding one line here. No engine changes required.

#include <cstdint>
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
    };
    return v;
}

} // namespace gno
