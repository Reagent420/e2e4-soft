#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <vector>

#include "../core/session_history.h"

class QWidget;

namespace gno {
namespace report {

// Renders rich PNG reports with QPainter — no extra dependencies.

QString defaultReportsDir();

// Live monitoring report: three charts + summary + recommendations
bool exportMonitoringReport(const QString& path,
                            const QVector<double>& ping,
                            const QVector<double>& jitter,
                            const QVector<double>& loss,
                            double avgPing, double avgJitter, double avgLoss,
                            const QStringList& recommendations);

// Before/after comparison report (comparison measurement on dashboard)
bool exportComparisonReport(const QString& path,
                            const QString& labelBefore, double pingBefore, double jitterBefore, double lossBefore, uint32_t samplesBefore,
                            const QString& labelAfter, double pingAfter, double jitterAfter, double lossAfter, uint32_t samplesAfter);

// Session history report: stats + table of recent sessions
bool exportHistoryReport(const QString& path,
                         const std::vector<SessionRecord>& records,
                         double avgPing, double avgJitter);

// Renders any widget to a PNG file
bool exportWidgetScreenshot(QWidget* widget, const QString& path);

} // namespace report
} // namespace gno