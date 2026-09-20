#pragma once

#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QProgressBar>
#include <QTabWidget>
#include <QLineEdit>
#include <QTextEdit>
#include <QVBoxLayout>
#include <atomic>

#include "../core/launch_diagnostics.h"
#include "../core/problem_db.h"
#include "../diagnostics/diagnostic_types.h"
#include "../diagnostics/diagnostics_service.h"

namespace gno {

// Диагностика запуска игры: результаты проверок, частые проблемы и
// матрица возможностей — разнесены по внутренним вкладкам, чтобы
// ничего не накладывалось друг на друга.
class DiagnosticsWidget : public QWidget {
    Q_OBJECT
public:
    explicit DiagnosticsWidget(QWidget* parent = nullptr);

public slots:
    void runDiagnostics(const QString& gameName = QString(), const QString& processName = QString());

private slots:
    void onGameSelected(int index);
    void onSearchChanged(const QString& text);
    void onPlainLanguageClicked();
    void renderResults(const GameDiagnostics& diag);
    void renderProblems(const QString& gameName);
    void renderCapabilities();

    // Маршрутная диагностика (DiagnosticsService): запуск с отменой и
    // рендер DiagnosticReport на русском.
    void runRouteDiagnostics();
    void cancelRouteDiagnostics();
    void renderRouteReport(const DiagnosticReport& report);
    void renderRouteError(DiagnosticError error, const std::vector<std::string>& evidence);
    // Сохраняет последний диагноз маршрута в Markdown/HTML/PDF.
    void exportRouteReport();

private:
    void setupUI();
    void setupRouteTab();
    void setRunningUi(bool running);
    void setRouteRunningUi(bool running);
    QWidget* makeCheckCard(const DiagnosticCheck& c);
    QWidget* makeProblemCard(const ProblemEntry& e);

    QComboBox* m_gameCombo_ = nullptr;
    QLabel* m_summaryLabel_ = nullptr;
    QWidget* m_resultsList_ = nullptr;
    QWidget* m_problemsList_ = nullptr;
    QWidget* m_capabilitiesList_ = nullptr;
    QPushButton* m_runBtn_ = nullptr;
    QPushButton* m_exportBtn_ = nullptr;
    QTabWidget* m_innerTabs_ = nullptr;
    QProgressBar* m_progress_ = nullptr;
    QLabel* m_runningLabel_ = nullptr;
    QLabel* m_adminLabel_ = nullptr;
    QLineEdit* m_problemSearch_ = nullptr;
    QTextEdit* m_plainText_ = nullptr;
    std::atomic<bool> m_running_{false};
    QString m_lastGame;
    QString m_lastProcess;
    QString m_lastGameName;
    QString m_problemQuery_;
    GameDiagnostics m_lastDiag_;
    bool m_hasDiag_ = false;

    // Вкладка «Маршрут до сервера»
    QPushButton* m_routeBtn_ = nullptr;
    QPushButton* m_routeCancelBtn_ = nullptr;
    QPushButton* m_routeExportBtn_ = nullptr;
    QLabel* m_routeOutcomeLabel_ = nullptr;
    QTextEdit* m_routeEvidence_ = nullptr;
    QLabel* m_routeStatusLabel_ = nullptr;
    std::atomic<bool> m_routeRunning_{false};
    // Последний собранный диагноз — источник для экспорта. Кнопка экспорта
    // активна только когда он есть: выгружать пустой отчёт бессмысленно.
    DiagnosticReport m_lastRouteReport_;
    bool m_hasRouteReport_ = false;
    // Источник отмены перезапускается на каждый запуск: токен живёт в
    // рабочем потоке, а cancel() дёргает кнопка из потока интерфейса.
    CancellationSource m_routeCancel_;
};

} // namespace gno
