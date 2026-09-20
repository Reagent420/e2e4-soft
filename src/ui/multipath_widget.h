#pragma once

#include <QWidget>
#include <QPointer>

#include "../core/multipath_engine.h"

class QTableWidget;
class QTableWidgetItem;
class QLineEdit;
class QCheckBox;
class QPushButton;
class QLabel;

namespace gno {

// GUI-мост к MultipathEngine: показывает список путей (один путь = один
// шлюз локального интерфейса), подсвечивает активный, даёт вкл/выкл
// автовыбор пути и уведомляет о переключениях. Модель та же, что у консольного
// режима --multipath, только вывод идёт в таблицу, а не в std::cout.
class MultipathWidget : public QWidget {
    Q_OBJECT
public:
    explicit MultipathWidget(QWidget* parent = nullptr);
    ~MultipathWidget() override;

signals:
    // Уведомление о переключении активного пути (для статус-бара/трея).
    void pathSwitched(const QString& summary);
    void engineRunningChanged(bool running);

private slots:
    void onStart();
    void onStop();
    void onAutoSwitchToggled(bool on);
    // Метрики пришли из движка (поток зондирования) — перерисуем таблицу.
    void onMetricsUpdated(const std::vector<PathMetrics>& paths);
    // Движок сменил активный путь — подсветка + уведомление.
    void onPathSwitched(uint32_t old_id, uint32_t new_id);

private:
    void setupUI();
    void setRunningUi(bool running);
    void refreshPathRows(const std::vector<PathMetrics>& paths);
    static QString describePath(const PathMetrics& m, bool active);

    MultipathEngine engine_;
    QTableWidget* m_table_ = nullptr;
    QLineEdit* m_targetEdit_ = nullptr;
    QPushButton* m_startBtn_ = nullptr;
    QPushButton* m_stopBtn_ = nullptr;
    QCheckBox* m_autoSwitchCb_ = nullptr;
    QLabel* m_statusLabel_ = nullptr;
    bool m_running_ = false;
};

} // namespace gno
