#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <vector>

#include "../core/session_history.h"
#include "../core/report_text.h"
#include "../diagnostics/diagnostic_types.h"

class QWidget;

namespace gno {
namespace report {

// Renders rich PNG reports with QPainter — no extra dependencies.

QString defaultReportsDir();

// Форматы текстовых отчётов. PNG-отчёты выше остаются как были; эти три —
// чтобы отчёт можно было открыть в блокноте (Markdown), в браузере (HTML)
// или отправить в PDF.
enum class ReportFormat { Markdown, Html, Pdf };

// Диагностика маршрута в файл. Для Pdf берётся светлая версия разметки —
// тёмный фон на бумаге заливает страницу.
bool exportDiagnosticReport(const QString& path, const DiagnosticReport& report,
                            ReportFormat format);

// История сессий в файл (Markdown/HTML/PDF).
bool exportSessionHistoryText(const QString& path,
                              const std::vector<SessionRecord>& records,
                              double avgPing, double avgJitter, double avgLoss,
                              ReportFormat format);

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
                         double avgPing, double avgJitter, double avgLoss);

// Renders any widget to a PNG file
bool exportWidgetScreenshot(QWidget* widget, const QString& path);

} // namespace report
} // namespace gno