#pragma once

#include <QWidget>
#include <QListWidget>
#include <QTextBrowser>
#include <QString>
#include <vector>
#include "core/problem_db.h"

class QPushButton;

namespace gno {

// Секция Knowledge: база известных проблем (problem_db) с поиском
// и переходом к связанным решениям. Сигнал applyRequested несёт
// fix_action выбранной проблемы (deep-link в Optimizations).
class KnowledgeWidget : public QWidget {
    Q_OBJECT
public:
    explicit KnowledgeWidget(QWidget* parent = nullptr);

signals:
    void applyRequested(const QString& fix_action);

private:
    // Заполняет список готовой подборкой. Вынесено из конструктора, чтобы
    // подбор по диагнозу и обычный поиск использовали один и тот же путь.
    void showEntries(const std::vector<ProblemEntry>& entries);
    // Статьи под последний диагноз маршрута (MonitoringService::lastDiagnosticReport).
    void showDiagnosticMatches();

    QListWidget* m_list = nullptr;
    QTextBrowser* m_detail = nullptr;
    QPushButton* m_applyBtn = nullptr;
    std::vector<ProblemEntry> m_entries;
    QString m_currentFix;
};

} // namespace gno
