#pragma once

// Simple i18n: two-language string table (Russian default).
// Usage: tr(Label::AppName) returns QString in current language.
// Adding a language = adding one enum value + one case per label.

#include <QString>
#include <QSettings>

namespace gno {
namespace i18n {

enum class Lang { Russian = 0, English = 1 };

inline Lang current() {
    return static_cast<Lang>(QSettings().value("app/language", 0).toInt());
}

inline void setCurrent(Lang l) {
    QSettings().setValue("app/language", static_cast<int>(l));
}

// Label identifiers
enum class L {
    AppName,
    Overview,
    GameMode,
    MyGames,
    Diagnostics,
    OptimizationWin,
    LaunchGame,
    Help,
    MobileNet,
    History,
    EventsLog,
    SettingsNav,
    FineTune,
    ServerMap,
    NetworkTools,
    ProcessMonitor,
    Optimizer,
    Profiles,
    Monitoring,
    Games,
    StartScan,
    RunDiagnostics,
    CheckState,
    ApplyRecommended,
    RollbackLast,
    ProbeAll,
    ProbeSelected,
    Refresh,
    RamClean,
    ExportCsv,
    ExportJson,
    ImportProfile,
    ExportProfile,
    SaveProfile,
    Score,
    Status,
    Ready,
    AdminYes,
    AdminNo,
    RebootRequired,
    NoIssues,
    Best,
    // ... add more as needed
};

inline QString tr(L label) {
    const bool en = current() == Lang::English;
    switch (label) {
        case L::AppName:       return en ? "GNO — Game Network Optimizer" : "GNO — Оптимизатор игровых сетей";
        case L::Overview:      return en ? "Overview" : "Обзор";
        case L::GameMode:      return en ? "Game Mode" : "Режим игры";
        case L::MyGames:       return en ? "My Games" : "Мои игры";
        case L::Diagnostics:   return en ? "Diagnostics" : "Диагностика";
        case L::OptimizationWin: return en ? "Windows Tuning" : "Оптимизация Windows";
        case L::LaunchGame:    return en ? "Launch Game" : "Запуск игры";
        case L::Help:          return en ? "Help" : "Помощь";
        case L::MobileNet:     return en ? "Mobile Network" : "Мобильная сеть";
        case L::History:       return en ? "History" : "История";
        case L::EventsLog:     return en ? "Event Log" : "Журнал событий";
        case L::SettingsNav:   return en ? "Settings" : "Настройки";
        case L::FineTune:      return en ? "Fine Tuning" : "Тонкая настройка";
        case L::ServerMap:     return en ? "Server Map" : "Карта серверов";
        case L::NetworkTools:  return en ? "Network Tools" : "Сетевые утилиты";
        case L::ProcessMonitor:return en ? "Processes" : "Процессы";
        case L::Optimizer:     return en ? "Optimizer" : "Оптимизатор";
        case L::Profiles:      return en ? "Profiles" : "Профили игр";
        case L::Monitoring:    return en ? "Monitoring" : "Мониторинг";
        case L::Games:         return en ? "Games" : "Игры";
        case L::StartScan:     return en ? "Scan processes" : "Сканировать процессы";
        case L::RunDiagnostics:return en ? "Run diagnostics" : "Запустить диагностику";
        case L::CheckState:    return en ? "Check state" : "Проверить состояние";
        case L::ApplyRecommended: return en ? "Apply recommended" : "Применить рекомендации";
        case L::RollbackLast:  return en ? "Rollback last" : "Откатить последнее";
        case L::ProbeAll:      return en ? "PROBE ALL" : "ПРОБОВАТЬ ВСЕ";
        case L::ProbeSelected: return en ? "PROBE SELECTED" : "ПРОБОВАТЬ ВЫБРАННЫЙ";
        case L::Refresh:       return en ? "Refresh" : "Обновить";
        case L::RamClean:      return en ? "Clean RAM" : "Очистить RAM";
        case L::ExportCsv:     return en ? "Export CSV" : "Экспорт CSV";
        case L::ExportJson:    return en ? "Export report (JSON)" : "Экспорт отчёта (JSON)";
        case L::ImportProfile: return en ? "Import profile" : "Импорт профиля";
        case L::ExportProfile: return en ? "Export profile" : "Экспорт профиля";
        case L::SaveProfile:   return en ? "Save profile" : "Сохранить профиль";
        case L::Score:         return en ? "Score" : "Оценка";
        case L::Status:        return en ? "Status" : "Статус";
        case L::Ready:         return en ? "Ready" : "Готов";
        case L::AdminYes:      return en ? "Administrator: yes — all optimizations available" : "Администратор: да — все оптимизации доступны";
        case L::AdminNo:       return en ? "Administrator: no — some fixes unavailable. Run as administrator." : "Администратор: нет — часть исправлений недоступна.";
        case L::RebootRequired: return en ? "Reboot required" : "Требуется перезагрузка";
        case L::NoIssues:      return en ? "No issues found" : "Проблем не найдено";
        case L::Best:          return en ? "BEST" : "ЛУЧШИЙ";
    }
    return QStringLiteral("?");
}

} // namespace i18n
} // namespace gno
