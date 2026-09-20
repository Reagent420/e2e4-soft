#pragma once

// Centralized settings keys — prevents typos in scattered QSettings calls.
// Usage: QSettings().setValue(SettingsKeys::Autopilot(), true);
//
// ВАЖНО: одни и те же настройки читают И пишут разные фронтинды (main_gui.cpp
// строит BoostRequest, optimizer.cpp рисует галочки, monitoring_service
// проверяет автопилот). Пока ключи были строковыми литералами, опечатка в
// одном месте молча рассинхронизировала GUI с самим собой: галочка показывала
// одно, а буст применял другое. Все ключи — только отсюда.

#include <QString>

namespace gno {
namespace SettingsKeys {

// --- remediation / scheduler ---
inline QString Autopilot()        { return QStringLiteral("remediation/autopilot"); }
inline QString EveningCheck()     { return QStringLiteral("scheduler/evening"); }
inline QString EveningTime()      { return QStringLiteral("scheduler/eveningTime"); }
inline QString MorningScan()      { return QStringLiteral("scheduler/morningScan"); }
// Дата (yyyy-MM-dd) последнего планового вечернего замера. Нужна, чтобы замер
// выполнялся один раз в сутки, а не при каждом запуске приложения.
inline QString LastEvening()      { return QStringLiteral("scheduler/lastEvening"); }

// --- optimizer/boost options (read by main_gui.cpp + optimizer.cpp) ---
inline QString GameDvr()          { return QStringLiteral("optimizer/gameDvr"); }
inline QString FullscreenOpt()    { return QStringLiteral("optimizer/fullscreenOpt"); }
inline QString MouseAccel()       { return QStringLiteral("optimizer/mouseAccel"); }
inline QString GameMode()         { return QStringLiteral("optimizer/gameMode"); }
inline QString PowerPlan()        { return QStringLiteral("optimizer/powerPlan"); }
inline QString HighPriority()     { return QStringLiteral("optimizer/highPriority"); }
inline QString VirtualMemory()    { return QStringLiteral("optimizer/virtualMemory"); }
inline QString Multipath()        { return QStringLiteral("optimizer/multipath"); }
inline QString RealTimeRoute()    { return QStringLiteral("optimizer/realTimeRoute"); }
inline QString LossCompensation() { return QStringLiteral("optimizer/lossCompensation"); }
inline QString CustomDns()        { return QStringLiteral("optimizer/customDns"); }
inline QString TcpOpt()           { return QStringLiteral("optimizer/tcpOpt"); }
inline QString MtuOpt()           { return QStringLiteral("optimizer/mtuOpt"); }
inline QString DnsServer()        { return QStringLiteral("optimizer/dnsServer"); }
inline QString MtuValue()         { return QStringLiteral("optimizer/mtuValue"); }

// --- map ---
inline QString MapRegion()        { return QStringLiteral("map/region"); }
inline QString MapLabels()        { return QStringLiteral("map/labels"); }
inline QString MapGrid()          { return QStringLiteral("map/grid"); }
inline QString MapInterval()      { return QStringLiteral("map/interval"); }
// Группа, а не ключ: внутри лежат latency-замеры по IP-адресам серверов
// (имя ключа = сам IP). beginGroup/endGroup — единственный способ работы.
inline QString MapProbeGroup()    { return QStringLiteral("map/probe"); }

// --- diagnostics / game selection ---
inline QString SelectedGameId()   { return QStringLiteral("game/selectedId"); }
inline QString SamplesCount()     { return QStringLiteral("diagnostics/samples"); }

// --- per-game alert thresholds (game_profiles_widget) ---
// Ключи динамические: thresholds/<game>/ping и .../loss.
inline QString ThresholdPing(const QString& game) {
    return QStringLiteral("thresholds/%1/ping").arg(game);
}
inline QString ThresholdLoss(const QString& game) {
    return QStringLiteral("thresholds/%1/loss").arg(game);
}

// --- appearance / UX ---
inline QString Theme()            { return QStringLiteral("theme"); }
inline QString SoundNotifications() { return QStringLiteral("soundNotifications"); }
inline QString OverlayEnabled()   { return QStringLiteral("overlayEnabled"); }
inline QString OverlayCorner()    { return QStringLiteral("overlayCorner"); }
inline QString OverlayOpacity()   { return QStringLiteral("overlayOpacity"); }
inline QString Language()         { return QStringLiteral("language"); }
inline QString FontSize()         { return QStringLiteral("app/fontSize"); }

// --- settings page (прочие галочки страницы настроек) ---
inline QString StartWithWindows() { return QStringLiteral("startWithWindows"); }
inline QString MinimizeToTray()   { return QStringLiteral("minimizeToTray"); }
inline QString Notifications()    { return QStringLiteral("notifications"); }
inline QString Protocol()         { return QStringLiteral("protocol"); }
inline QString Region()           { return QStringLiteral("region"); }
inline QString MaxRoutes()        { return QStringLiteral("maxRoutes"); }
inline QString PingInterval()     { return QStringLiteral("pingInterval"); }
inline QString VerboseLog()       { return QStringLiteral("verboseLog"); }
inline QString AutoUpdate()       { return QStringLiteral("autoUpdate"); }
inline QString DevMode()          { return QStringLiteral("devMode"); }
// Тема: ключи палитры лежат в группе theme/<name> — theme_editor удаляет
// их по одному (QSettings::remove группы не умеет).
inline QString ThemeColor(const QString& name) { return QStringLiteral("theme/%1").arg(name); }

} // namespace SettingsKeys
} // namespace gno
