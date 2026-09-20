#include "report_text.h"

#include "core/plain_language.h"

#include <cstdio>
#include <ctime>

namespace gno {
namespace report {
namespace {

std::tm localTime(std::time_t t) {
    std::tm out{};
#if defined(_WIN32)
    localtime_s(&out, &t);
#else
    localtime_r(&t, &out);
#endif
    return out;
}

std::string nowString() {
    const std::time_t t = std::time(nullptr);
    const std::tm tm = localTime(t);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min);
    return buf;
}

std::string fmt(double value) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", value);
    return buf;
}

const char* protocolLabel(TransportProtocol protocol) {
    return protocol == TransportProtocol::Tcp ? "TCP" : "UDP";
}

std::string endpointString(const ObservedEndpoint& endpoint) {
    if (endpoint.ip.isUnspecified()) return "не определена";
    std::string out = endpoint.ip.toString();
    out += ':';
    out += std::to_string(endpoint.port);
    out += " (";
    out += protocolLabel(endpoint.protocol);
    out += ')';
    return out;
}

std::string effectiveTime(const ReportHeader& header) {
    return header.generated_at.empty() ? nowString() : header.generated_at;
}

std::string metricRowMarkdown(const char* label, const MetricSummary& m) {
    std::string row = "| ";
    row += label;
    row += " | ";
    row += std::to_string(m.sent);
    row += " | ";
    row += std::to_string(m.received);
    row += " | ";
    row += fmt(m.median_ms);
    row += " | ";
    row += fmt(m.p95_ms);
    row += " | ";
    row += fmt(m.jitter_ms);
    row += " | ";
    row += fmt(m.loss_percent);
    row += " |\n";
    return row;
}

std::string metricRowHtml(const char* label, const MetricSummary& m) {
    std::string row = "<tr><td>";
    row += label;
    row += "</td><td>";
    row += std::to_string(m.sent);
    row += "</td><td>";
    row += std::to_string(m.received);
    row += "</td><td>";
    row += fmt(m.median_ms);
    row += "</td><td>";
    row += fmt(m.p95_ms);
    row += "</td><td>";
    row += fmt(m.jitter_ms);
    row += "</td><td>";
    row += fmt(m.loss_percent);
    row += "</td></tr>\n";
    return row;
}

const char* kMetricHeaderMarkdown =
    "| Канал | Отправлено | Получено | Медиана, мс | p95, мс | Джиттер, мс | Потери, % |\n"
    "|---|---|---|---|---|---|---|\n";

const char* kMetricHeaderHtml =
    "<tr><th>Канал</th><th>Отправлено</th><th>Получено</th><th>Медиана, мс</th>"
    "<th>p95, мс</th><th>Джиттер, мс</th><th>Потери, %</th></tr>\n";

const char* kHtmlHeadDark =
    "<!DOCTYPE html>\n<html lang=\"ru\">\n<head>\n<meta charset=\"utf-8\">\n"
    "<title>E2E4 Soft — отчёт</title>\n<style>\n"
    "body{background:#0A0E17;color:#EAF6FF;font-family:'Segoe UI',sans-serif;"
    "margin:0;padding:32px;line-height:1.5}\n"
    "h1{color:#22D3EE;font-size:22px;margin:0 0 4px}\n"
    "h2{color:#22D3EE;font-size:16px;margin:28px 0 8px;border-bottom:1px solid "
    "rgba(0,240,255,0.14);padding-bottom:4px}\n"
    "table{border-collapse:collapse;margin:8px 0}\n"
    "th,td{border:1px solid rgba(0,240,255,0.14);padding:6px 12px;text-align:left;"
    "font-size:13px}\n"
    "th{background:#101A2B;color:#9DB8D6}\n"
    "li{margin:2px 0}\n"
    ".muted{color:#9DB8D6;font-size:13px}\n"
    "</style>\n</head>\n<body>\n";

// Светлая версия — для печати и PDF: тёмный фон на бумаге заливает страницу,
// а QTextDocument в PDF тёмную подложку всё равно не отрисует.
const char* kHtmlHeadPrint =
    "<!DOCTYPE html>\n<html lang=\"ru\">\n<head>\n<meta charset=\"utf-8\">\n"
    "<title>E2E4 Soft — отчёт</title>\n<style>\n"
    "body{background:#FFFFFF;color:#0F172A;font-family:'Segoe UI',sans-serif;"
    "margin:0;padding:0;line-height:1.5}\n"
    "h1{color:#0E7490;font-size:20px;margin:0 0 4px}\n"
    "h2{color:#0E7490;font-size:15px;margin:22px 0 8px;border-bottom:1px solid "
    "#CBD5E1;padding-bottom:4px}\n"
    "table{border-collapse:collapse;margin:8px 0}\n"
    "th,td{border:1px solid #CBD5E1;padding:6px 12px;text-align:left;font-size:12px}\n"
    "th{background:#F1F5F9;color:#475569}\n"
    "li{margin:2px 0}\n"
    ".muted{color:#64748B;font-size:12px}\n"
    "</style>\n</head>\n<body>\n";

const char* htmlHead(bool for_print) {
    return for_print ? kHtmlHeadPrint : kHtmlHeadDark;
}

} // namespace

std::string escapeHtml(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
            case '&':  out += "&amp;"; break;
            case '<':  out += "&lt;"; break;
            case '>':  out += "&gt;"; break;
            case '"':  out += "&quot;"; break;
            default:   out += c; break;
        }
    }
    return out;
}

std::string escapeMarkdownCell(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        // Вертикальная черта — разделитель ячеек; перевод строки разваливает
        // строку таблицы. Оба экранируем.
        if (c == '|') out += "\\|";
        else if (c == '\n' || c == '\r') out += ' ';
        else out += c;
    }
    return out;
}

std::string diagnosticReportToMarkdown(const DiagnosticReport& report,
                                       const ReportHeader& header) {
    const std::string outcome = PlainLanguageReport::outcomeLabel(report.outcome);
    const std::string confidence = PlainLanguageReport::confidenceLabel(report.confidence);
    const std::string recommendation = PlainLanguageReport::diagnosticRecommendation(report);

    std::string md = "# Диагностика маршрута — E2E4 Soft\n\n";
    if (!header.app_version.empty()) md += "- **Версия:** " + header.app_version + "\n";
    md += "- **Сформировано:** " + effectiveTime(header) + "\n";
    if (!report.game_id.empty())
        md += "- **Игра:** " + escapeMarkdownCell(report.game_id) + "\n";
    md += "\n## Итог\n\n";
    md += "- **Исход:** " + outcome + "\n";
    md += "- **Уверенность:** " + confidence + "\n";
    md += "- **Конечная точка:** " + endpointString(report.endpoint) + "\n";
    if (!recommendation.empty()) md += "\n" + recommendation + "\n";

    md += "\n## Метрики\n\n";
    md += kMetricHeaderMarkdown;
    md += metricRowMarkdown("До шлюза", report.gateway);
    md += metricRowMarkdown("До сервера игры", report.direct);

    if (!report.candidates.empty()) {
        md += "\n## Кандидаты (релеи)\n\n";
        md += "| Регион | До релея, мс | Релей→игра, мс |\n|---|---|---|\n";
        for (const auto& c : report.candidates) {
            md += "| " + escapeMarkdownCell(c.probe_region) + " | " +
                  fmt(c.client_to_probe.median_ms) + " | " +
                  fmt(c.probe_to_game.median_ms) + " |\n";
        }
    }

    md += "\n## Доказательства\n\n";
    if (report.evidence.empty()) {
        md += "- нет данных\n";
    } else {
        for (const auto& e : report.evidence)
            md += "- " + escapeMarkdownCell(e) + "\n";
    }
    return md;
}

std::string diagnosticReportToHtml(const DiagnosticReport& report,
                                   const ReportHeader& header, bool for_print) {
    const std::string outcome = PlainLanguageReport::outcomeLabel(report.outcome);
    const std::string confidence = PlainLanguageReport::confidenceLabel(report.confidence);
    const std::string recommendation = PlainLanguageReport::diagnosticRecommendation(report);

    std::string html = htmlHead(for_print);
    html += "<h1>Диагностика маршрута — E2E4 Soft</h1>\n";
    html += "<p class=\"muted\">";
    if (!header.app_version.empty())
        html += "Версия " + escapeHtml(header.app_version) + " · ";
    html += "Сформировано " + escapeHtml(effectiveTime(header)) + "</p>\n";

    html += "<h2>Итог</h2>\n<ul>\n";
    html += "<li><b>Исход:</b> " + escapeHtml(outcome) + "</li>\n";
    html += "<li><b>Уверенность:</b> " + escapeHtml(confidence) + "</li>\n";
    if (!report.game_id.empty())
        html += "<li><b>Игра:</b> " + escapeHtml(report.game_id) + "</li>\n";
    html += "<li><b>Конечная точка:</b> " + escapeHtml(endpointString(report.endpoint)) + "</li>\n";
    html += "</ul>\n";
    if (!recommendation.empty())
        html += "<p>" + escapeHtml(recommendation) + "</p>\n";

    html += "<h2>Метрики</h2>\n<table>\n";
    html += kMetricHeaderHtml;
    html += metricRowHtml("До шлюза", report.gateway);
    html += metricRowHtml("До сервера игры", report.direct);
    html += "</table>\n";

    if (!report.candidates.empty()) {
        html += "<h2>Кандидаты (релеи)</h2>\n<table>\n";
        html += "<tr><th>Регион</th><th>До релея, мс</th><th>Релей→игра, мс</th></tr>\n";
        for (const auto& c : report.candidates) {
            html += "<tr><td>" + escapeHtml(c.probe_region) + "</td><td>" +
                    fmt(c.client_to_probe.median_ms) + "</td><td>" +
                    fmt(c.probe_to_game.median_ms) + "</td></tr>\n";
        }
        html += "</table>\n";
    }

    html += "<h2>Доказательства</h2>\n";
    if (report.evidence.empty()) {
        html += "<p class=\"muted\">нет данных</p>\n";
    } else {
        html += "<ul>\n";
        for (const auto& e : report.evidence)
            html += "<li>" + escapeHtml(e) + "</li>\n";
        html += "</ul>\n";
    }

    html += "</body>\n</html>\n";
    return html;
}

std::string sessionHistoryToMarkdown(const std::vector<SessionRecord>& records,
                                     double avgPing, double avgJitter, double avgLoss,
                                     const ReportHeader& header) {
    std::string md = "# История сессий — E2E4 Soft\n\n";
    if (!header.app_version.empty()) md += "- **Версия:** " + header.app_version + "\n";
    md += "- **Сформировано:** " + effectiveTime(header) + "\n";
    md += "- **Сессий:** " + std::to_string(records.size()) + "\n";
    md += "- **Средние:** задержка " + fmt(avgPing) + " мс, джиттер " + fmt(avgJitter) +
          " мс, потери " + fmt(avgLoss) + " %\n";

    md += "\n| Игра | Начало | Конец | Длит., с | Задержка, мс | Джиттер, мс | Потери, % | Оценка | Буст |\n";
    md += "|---|---|---|---|---|---|---|---|---|\n";
    if (records.empty()) {
        md += "| — | — | — | 0 | 0.00 | 0.00 | 0.00 | — | — |\n";
    } else {
        for (const auto& r : records) {
            md += "| " + escapeMarkdownCell(r.game_name) +
                  " | " + escapeMarkdownCell(r.start_time_str) +
                  " | " + escapeMarkdownCell(r.end_time_str) +
                  " | " + std::to_string(r.duration_seconds) +
                  " | " + fmt(r.avg_ping_ms) +
                  " | " + fmt(r.avg_jitter_ms) +
                  " | " + fmt(r.avg_packet_loss) +
                  " | " + fmt(r.quality_score) +
                  " | " + (r.boost_was_active ? "да" : "нет") + " |\n";
        }
    }
    return md;
}

std::string sessionHistoryToHtml(const std::vector<SessionRecord>& records,
                                 double avgPing, double avgJitter, double avgLoss,
                                 const ReportHeader& header, bool for_print) {
    std::string html = htmlHead(for_print);
    html += "<h1>История сессий — E2E4 Soft</h1>\n";
    html += "<p class=\"muted\">";
    if (!header.app_version.empty())
        html += "Версия " + escapeHtml(header.app_version) + " · ";
    html += "Сформировано " + escapeHtml(effectiveTime(header)) + "</p>\n";

    html += "<h2>Сводка</h2>\n<ul>\n";
    html += "<li><b>Сессий:</b> " + std::to_string(records.size()) + "</li>\n";
    html += "<li><b>Средняя задержка:</b> " + fmt(avgPing) + " мс</li>\n";
    html += "<li><b>Средний джиттер:</b> " + fmt(avgJitter) + " мс</li>\n";
    html += "<li><b>Средние потери:</b> " + fmt(avgLoss) + " %</li>\n";
    html += "</ul>\n";

    html += "<h2>Сессии</h2>\n<table>\n";
    html += "<tr><th>Игра</th><th>Начало</th><th>Конец</th><th>Длит., с</th>"
            "<th>Задержка, мс</th><th>Джиттер, мс</th><th>Потери, %</th>"
            "<th>Оценка</th><th>Буст</th></tr>\n";
    if (records.empty()) {
        html += "<tr><td colspan=\"9\" class=\"muted\">нет записей</td></tr>\n";
    } else {
        for (const auto& r : records) {
            html += "<tr><td>" + escapeHtml(r.game_name) +
                    "</td><td>" + escapeHtml(r.start_time_str) +
                    "</td><td>" + escapeHtml(r.end_time_str) +
                    "</td><td>" + std::to_string(r.duration_seconds) +
                    "</td><td>" + fmt(r.avg_ping_ms) +
                    "</td><td>" + fmt(r.avg_jitter_ms) +
                    "</td><td>" + fmt(r.avg_packet_loss) +
                    "</td><td>" + fmt(r.quality_score) +
                    "</td><td>" + (r.boost_was_active ? "да" : "нет") + "</td></tr>\n";
        }
    }
    html += "</table>\n</body>\n</html>\n";
    return html;
}

} // namespace report
} // namespace gno
