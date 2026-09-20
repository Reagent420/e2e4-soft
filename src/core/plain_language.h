#pragma once

#include "core/launch_diagnostics.h"
#include "diagnostics/diagnostic_types.h"

#include <string>
#include <vector>

namespace gno {

struct PlainSection {
    std::string title;
    std::vector<std::string> lines;
};

// Builds the "human view": what is wrong, what we can do, what the user
// should do themselves, and what GNO deliberately does not do.
class PlainLanguageReport {
public:
    static std::vector<PlainSection> build(double avg_rtt_ms, double jitter_ms,
                                           double packet_loss_percent, bool network_ok,
                                           const GameDiagnostics* launch, bool elevated);

    // Пересказ DiagnosisService на русском: исход, уверенность и доказательства.
    // evidence_out (если задан) заполняется строками «· ...» для виджета.
    static std::string formatDiagnosticReport(const DiagnosticReport& report,
                                              std::vector<std::string>* evidence_out = nullptr);

    // Краткая рекомендация из диагноза маршрута для MonitoringService.
    // Возвращает пустую строку, если исход не actionable (InsufficientData) —
    // нет смысла советовать то, что нечем подкрепить.
    static std::string diagnosticRecommendation(const DiagnosticReport& report);

    // Названия элементов перечислений диагностики на русском.
    static std::string outcomeLabel(DiagnosticOutcome outcome);
    static std::string confidenceLabel(ConfidenceLevel level);
    static std::string errorLabel(DiagnosticError error);
};

} // namespace gno
