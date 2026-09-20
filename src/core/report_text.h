#pragma once

// Генераторы текстовых отчётов: DiagnosticReport и история сессий в
// Markdown/HTML. Существующий report_export рисует только PNG через QPainter —
// его нельзя ни открыть в блокноте, ни приложить к письму, ни положить в тикет.
//
// Модуль Qt-free (GNO-tests не линкует Qt), поэтому тексты собираются здесь, а
// UI-слой только записывает готовую строку в файл и при необходимости печатает
// её в PDF через QPdfWriter.

#include <string>
#include <vector>

#include "diagnostics/diagnostic_types.h"
#include "core/session_history.h"

namespace gno {
namespace report {

// Шапка отчёта. Версия передаётся снаружи: core не знает про ui/theme.h.
struct ReportHeader {
    std::string app_version;   // «3.1.0»
    std::string generated_at;  // «2026-09-20 18:59»; пусто → текущее местное время
};

// Экранирование &, <, >, " для вставки в HTML. Без него адрес или название
// игры с «<» ломали бы разметку.
std::string escapeHtml(const std::string& text);

// Экранирование «|» для ячеек Markdown-таблиц.
std::string escapeMarkdownCell(const std::string& text);

std::string diagnosticReportToMarkdown(const DiagnosticReport& report,
                                       const ReportHeader& header = {});
// for_print=true переключает встроенный CSS на светлый: тёмный фон на бумаге
// и в PDF нечитаем (принтер заливает страницу).
std::string diagnosticReportToHtml(const DiagnosticReport& report,
                                   const ReportHeader& header = {},
                                   bool for_print = false);

std::string sessionHistoryToMarkdown(const std::vector<SessionRecord>& records,
                                     double avgPing, double avgJitter, double avgLoss,
                                     const ReportHeader& header = {});
std::string sessionHistoryToHtml(const std::vector<SessionRecord>& records,
                                 double avgPing, double avgJitter, double avgLoss,
                                 const ReportHeader& header = {},
                                 bool for_print = false);

} // namespace report
} // namespace gno
