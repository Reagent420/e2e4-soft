#pragma once

#include <QWidget>
#include "../core/connection_grader.h"

class QChartView;
#include <QVBoxLayout>
#include <QLabel>
#include <QTimer>

namespace gno {

class SessionHistory;

class SessionHistoryWidget : public QWidget {
    Q_OBJECT

public:
    explicit SessionHistoryWidget(QWidget* parent = nullptr);

private slots:
    void refreshHistory();

private:
    void setupUI();

    SessionHistory* m_history;
    QWidget* m_sessionList;
    QChartView* m_chart = nullptr;
    QLabel* m_statsLabel;
};

} // namespace gno
