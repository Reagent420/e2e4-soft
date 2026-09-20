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

    // Единственный владелец истории — MonitoringService. Раньше виджет
    // создавал свой собственный SessionHistory на том же файле history.json:
    // кнопка «Очистить» затирала файл, а сервис при выходе сохранял свою
    // копию — удалённые сессии воскресали при следующем запуске.
    SessionHistory* m_history;
    QWidget* m_sessionList;
    QChartView* m_chart = nullptr;
    QLabel* m_statsLabel;
};

} // namespace gno
