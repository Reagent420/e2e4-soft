#pragma once

#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QLineEdit>
#include <QTextEdit>
#include <QVBoxLayout>
#include <atomic>

#include "../core/launch_diagnostics.h"
#include "../core/problem_db.h"

namespace gno {

// Live game-launch diagnostics, known-problems database and capability matrix.
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

private:
    void setupUI();
    QWidget* makeCheckCard(const DiagnosticCheck& c);
    QWidget* makeProblemCard(const ProblemEntry& e);

    QComboBox* m_gameCombo_;
    QLabel* m_summaryLabel_;
    QWidget* m_resultsList_;
    QWidget* m_problemsList_;
    QWidget* m_capabilitiesList_;
    QPushButton* m_runBtn_;
    QLabel* m_runningLabel_;
    QLabel* m_adminLabel_;
    QLineEdit* m_problemSearch_;
    QTextEdit* m_plainText_;
    std::atomic<bool> m_running_{false};
    QString m_lastGame;
    QString m_lastProcess;
    QString m_lastGameName;
    QString m_problemQuery_;
    GameDiagnostics m_lastDiag_;
    bool m_hasDiag_ = false;
};

} // namespace gno