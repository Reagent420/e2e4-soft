#pragma once

#include <QWidget>
#include <QVector>
#include <QTimer>
#include <QLabel>
#include <QPushButton>

#include "../monitoring/ping_monitor.h"

namespace gno {

class PingGraphWidget : public QWidget {
    Q_OBJECT
public:
    explicit PingGraphWidget(QWidget* parent = nullptr);
    void addPingValue(double ms);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QVector<double> data_;
    static constexpr int kMaxPoints = 60;
};

class DashboardWidget : public QWidget {
    Q_OBJECT
public:
    explicit DashboardWidget(QWidget* parent = nullptr);
    ~DashboardWidget() override;

    void setConnected(bool connected);

signals:
    void boostToggled(bool active);

private slots:
    void onBoostClicked();
    void onMeasureClicked();
    void onMeasureTick();
    void onPingUpdated(double ms);
    void onJitterUpdated(double ms);
    void onLossUpdated(double percent);
    void refreshRecommendations();

private:
    QWidget* createMetricCard(const QString& label, QLabel** valueOut, QLabel** deltaOut, const QString& deltaColor);
    void setMetricColor(QLabel* label, double value, double good, double warn);
    void saveReportDialog(const QString& path);

    PingGraphWidget* graph_;
    QLabel* ping_value_;
    QLabel* jitter_value_;
    QLabel* loss_value_;
    QLabel* route_value_;
    QLabel* status_value_;
    QPushButton* boost_btn_;
    QPushButton* measure_btn_;
    QPushButton* ram_btn_ = nullptr;
    QLabel* ram_label_ = nullptr;
    QLabel* timer_label_ = nullptr;
    QLabel* startup_label_ = nullptr;
    QLabel* rec_card_;
    QTimer* measure_timer_;
    bool boosting_ = false;

    double last_ping_ = 0.0;

    // comparison measurement state machine
    int measure_phase_ = 0;   // 0 = idle, 1 = before, 2 = after
    int measure_seconds_ = 0;
    static constexpr int kMeasureDuration = 30;
    QVector<double> measure_before_;
    QVector<double> measure_after_;
};

} // namespace gno