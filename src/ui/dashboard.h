#pragma once

#include <QWidget>
#include <QVector>
#include <QTimer>
#include <QLabel>

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
    explicit DashboardWidget(QWidget* parent = nullptr, bool startMonitoring = true);
    ~DashboardWidget() override;

private:
    void onPingResult(const gno::ICMPResult& result);
    void startMonitoring();

    PingGraphWidget* graph_;
    QLabel* ping_value_;
    QLabel* jitter_value_;
    QLabel* loss_value_;
    QLabel* route_value_;
    QLabel* status_value_;
    gno::PingMonitor ping_monitor_;
    QVector<double> jitter_history_;
    QVector<double> loss_history_;
    double last_ping_ = 0.0;
    uint32_t packets_sent_ = 0;
    uint32_t packets_lost_ = 0;

    QWidget* createMetricCard(const QString& label, QLabel** valueOut, QLabel** deltaOut, const QString& deltaColor);
};

} // namespace gno
