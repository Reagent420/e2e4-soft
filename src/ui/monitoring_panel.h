#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QGroupBox>
#include <QChart>
#include <QChartView>
#include <QLineSeries>
#include <QValueAxis>
#include <QPushButton>
#include <QTimer>
#include "../monitoring/ping_monitor.h"
#include "../monitoring/packet_loss_monitor.h"
#include "../monitoring/jitter_calculator.h"
#include "../monitoring/stats_collector.h"

namespace gno {
namespace ui {

class MonitoringPanel : public QWidget {
    Q_OBJECT

public:
    explicit MonitoringPanel(QWidget* parent = nullptr);
    ~MonitoringPanel();

    void startMonitoring(const std::string& target_ip);
    void stopMonitoring();
    bool isMonitoring() const;

    void updateStats(const PingStats& ping, const PacketLossResult& loss, const JitterStats& jitter);

signals:
    void monitoringUpdated(double ping, double jitter, double loss);

private slots:
    void onRefreshStats();
    void onExportStats();
    void updateCharts();

private:
    void setupUI();
    void setupPingChart();
    void setupJitterChart();
    void setupLossChart();
    void updatePingDisplay(double ping_ms);
    void updateJitterDisplay(double jitter_ms);
    void updateLossDisplay(double loss_percent);

    QGroupBox* createStatGroup(const QString& title, QLabel** value_label, QLabel** unit_label);
    QChartView* createChartView(QChart* chart);

    QChart* ping_chart_;
    QChart* jitter_chart_;
    QChart* loss_chart_;
    
    QLineSeries* ping_series_;
    QLineSeries* jitter_series_;
    QLineSeries* loss_series_;
    
    QValueAxis* ping_axis_x_;
    QValueAxis* ping_axis_y_;
    QValueAxis* jitter_axis_x_;
    QValueAxis* jitter_axis_y_;
    QValueAxis* loss_axis_x_;
    QValueAxis* loss_axis_y_;
    
    QLabel* ping_value_label_;
    QLabel* jitter_value_label_;
    QLabel* loss_value_label_;
    QLabel* ping_min_label_;
    QLabel* ping_max_label_;
    QLabel* ping_avg_label_;
    
    QPushButton* refresh_button_;
    QPushButton* export_button_;
    
    QTimer* chart_timer_;
    
    PingMonitor ping_monitor_;
    PacketLossMonitor packet_loss_monitor_;
    JitterCalculator jitter_calculator_;
    StatsCollector stats_collector_;
    
    std::string target_ip_;
    uint32_t chart_max_points_ = 60;
};

} // namespace ui
} // namespace gno
