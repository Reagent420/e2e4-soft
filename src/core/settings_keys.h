#pragma once

// Centralized settings keys — prevents typos in scattered QSettings calls.
// Usage: QSettings().setValue(SettingsKeys::Autopilot(), true);

#include <QString>

namespace gno {
namespace SettingsKeys {

inline QString Autopilot()        { return QStringLiteral("remediation/autopilot"); }
inline QString EveningCheck()     { return QStringLiteral("scheduler/evening"); }
inline QString EveningTime()      { return QStringLiteral("scheduler/eveningTime"); }
inline QString MorningScan()      { return QStringLiteral("scheduler/morningScan"); }
inline QString MapRegion()        { return QStringLiteral("map/region"); }
inline QString MapLabels()        { return QStringLiteral("map/labels"); }
inline QString MapGrid()          { return QStringLiteral("map/grid"); }
inline QString MapInterval()      { return QStringLiteral("map/interval"); }
inline QString SelectedGameId()   { return QStringLiteral("game/selectedId"); }
inline QString SamplesCount()     { return QStringLiteral("diagnostics/samples"); }

} // namespace SettingsKeys
} // namespace gno
