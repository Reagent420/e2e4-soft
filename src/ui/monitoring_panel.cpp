#include "monitoring_panel.h"
#include <QGridLayout>
#include <QDateTime>

namespace gno {
namespace ui {

MonitoringPanel::MonitoringPanel(QWidget* parent)
    : QWidget(parent) {
    setupUI();
    
    chart_timer_ = new QTimer(this);
    connect(chart_timer_, &QTimer::timeout, this, &MonitoringPanel::updateCharts);
    chart_timer_->start(1000);
}

MonitoringPanel::~MonitoringPanel() {
    stopMonitoring();
}

void MonitoringPanel::startMonitoring(const std::string& target_ip) {
    target_ip_ = target_ip;
    ping_monitor_.start(target_ip);
    packet_loss_monitor_.start(target_ip);
}

void MonitoringPanel::stopMonitoring() {
    ping_monitor_.stop();
    packet_loss_monitor_.stop();
}

bool MonitoringPanel::isMonitoring() const {
    return ping_monitor_.isRunning();
}

void MonitoringPanel::updateStats(const PingStats& ping, const PacketLossResult& loss, const JitterStats& jitter) {
    updatePingDisplay(ping.current_latency_ms);
    updateJitterDisplay(jitter.current_jitter_ms);
    updateLossDisplay(loss.loss_percent);
    
    QDateTime now = QDateTime::currentDateTime();
    qint64 timestamp = now.toSecsSinceEpoch();
    
    ping_series_->append(timestamp, ping.current_latency_ms);
    jitter_series_->append(timestamp, jitter.current_jitter_ms);
    loss_series_->append(timestamp, loss.loss_percent);
    
    if (ping_series_->count() > chart_max_points_) {
        ping_series_->remove(0);
    }
    if (jitter_series_->count() > chart_max_points_) {
        jitter_series_->remove(0);
    }
    if (loss_series_->count() > chart_max_points_) {
        loss_series_->remove(0);
    }
    
    ping_min_label_->setText(QString("Min: %1 ms").arg(ping.min_latency_ms, 0, 'f', 1));
    ping_max_label_->setText(QString("Max: %1 ms").arg(ping.max_latency_ms, 0, 'f', 1));
    ping_avg_label_->setText(QString("Avg: %1 ms").arg(ping.avg_latency_ms, 0, 'f', 1));
    
    emit monitoringUpdated(ping.current_latency_ms, jitter.current_jitter_ms, loss.loss_percent);
}

void MonitoringPanel::setupUI() {
    QVBoxLayout* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(16, 16, 16, 16);
    main_layout->setSpacing(12);
    
    QLabel* title = new QLabel("Network Monitoring");
    title->setStyleSheet("font-size: 18px; font-weight: bold; color: #2a82da;");
    main_layout->addWidget(title);
    
    QHBoxLayout* stats_layout = new QHBoxLayout();
    
    QGroupBox* ping_group = createStatGroup("Ping", &ping_value_label_, nullptr);
    stats_layout->addWidget(ping_group);
    
    QGroupBox* jitter_group = createStatGroup("Jitter", &jitter_value_label_, nullptr);
    stats_layout->addWidget(jitter_group);
    
    QGroupBox* loss_group = createStatGroup("Packet Loss", &loss_value_label_, nullptr);
    stats_layout->addWidget(loss_group);
    
    main_layout->addLayout(stats_layout);
    
    QHBoxLayout* min_max_layout = new QHBoxLayout();
    ping_min_label_ = new QLabel("Min: --");
    ping_min_label_->setStyleSheet("color: #4ecdc4; font-size: 11px;");
    min_max_layout->addWidget(ping_min_label_);
    
    ping_max_label_ = new QLabel("Max: --");
    ping_max_label_->setStyleSheet("color: #ff6b6b; font-size: 11px;");
    min_max_layout->addWidget(ping_max_label_);
    
    ping_avg_label_ = new QLabel("Avg: --");
    ping_avg_label_->setStyleSheet("color: #ffd93d; font-size: 11px;");
    min_max_layout->addWidget(ping_avg_label_);
    
    main_layout->addLayout(min_max_layout);
    
    setupPingChart();
    setupJitterChart();
    setupLossChart();
    
    QGridLayout* charts_layout = new QGridLayout();
    charts_layout->addWidget(createChartView(ping_chart_), 0, 0);
    charts_layout->addWidget(createChartView(jitter_chart_), 0, 1);
    charts_layout->addWidget(createChartView(loss_chart_), 1, 0);
    
    main_layout->addLayout(charts_layout);
    
    QHBoxLayout* button_layout = new QHBoxLayout();
    refresh_button_ = new QPushButton("Refresh");
    connect(refresh_button_, &QPushButton::clicked, this, &MonitoringPanel::onRefreshStats);
    button_layout->addWidget(refresh_button_);
    
    export_button_ = new QPushButton("Export Stats");
    connect(export_button_, &QPushButton::clicked, this, &MonitoringPanel::onExportStats);
    button_layout->addWidget(export_button_);
    
    button_layout->addStretch();
    main_layout->addLayout(button_layout);
}

QGroupBox* MonitoringPanel::createStatGroup(const QString& title, QLabel** value_label, QLabel** unit_label) {
    QGroupBox* group = new QGroupBox(title);
    group->setStyleSheet("QGroupBox { font-size: 12px; }");
    
    QVBoxLayout* layout = new QVBoxLayout(group);
    layout->setAlignment(Qt::AlignCenter);
    
    *value_label = new QLabel("--");
    (*value_label)->setStyleSheet("font-size: 24px; font-weight: bold; color: #4ecdc4;");
    (*value_label)->setAlignment(Qt::AlignCenter);
    layout->addWidget(*value_label);
    
    if (unit_label) {
        *unit_label = new QLabel("ms");
        (*unit_label)->setStyleSheet("font-size: 10px; color: #888;");
        (*unit_label)->setAlignment(Qt::AlignCenter);
        layout->addWidget(*unit_label);
    }
    
    return group;
}

QChartView* MonitoringPanel::createChartView(QChart* chart) {
    QChartView* chart_view = new QChartView(chart);
    chart_view->setRenderHint(QPainter::Antialiasing);
    chart_view->setStyleSheet("background-color: #1a1a1a; border-radius: 4px;");
    return chart_view;
}

void MonitoringPanel::setupPingChart() {
    ping_chart_ = new QChart();
    ping_chart_->setTitle("Ping (ms)");
    ping_chart_->setTitleBrush(QBrush(QColor("#4ecdc4")));
    ping_chart_->setBackgroundBrush(QBrush(QColor("#1a1a1a")));
    ping_chart_->legend()->hide();
    
    ping_series_ = new QLineSeries();
    ping_series_->setPen(QPen(QColor("#4ecdc4"), 2));
    ping_chart_->addSeries(ping_series_);
    
    ping_axis_x_ = new QValueAxis();
    ping_axis_x_->setVisible(false);
    ping_chart_->addAxis(ping_axis_x_, Qt::AlignBottom);
    ping_series_->attachAxis(ping_axis_x_);
    
    ping_axis_y_ = new QValueAxis();
    ping_axis_y_->setLabelsColor(QColor("#888"));
    ping_chart_->addAxis(ping_axis_y_, Qt::AlignLeft);
    ping_series_->attachAxis(ping_axis_y_);
}

void MonitoringPanel::setupJitterChart() {
    jitter_chart_ = new QChart();
    jitter_chart_->setTitle("Jitter (ms)");
    jitter_chart_->setTitleBrush(QBrush(QColor("#ff6b6b")));
    jitter_chart_->setBackgroundBrush(QBrush(QColor("#1a1a1a")));
    jitter_chart_->legend()->hide();
    
    jitter_series_ = new QLineSeries();
    jitter_series_->setPen(QPen(QColor("#ff6b6b"), 2));
    jitter_chart_->addSeries(jitter_series_);
    
    jitter_axis_x_ = new QValueAxis();
    jitter_axis_x_->setVisible(false);
    jitter_chart_->addAxis(jitter_axis_x_, Qt::AlignBottom);
    jitter_series_->attachAxis(jitter_axis_x_);
    
    jitter_axis_y_ = new QValueAxis();
    jitter_axis_y_->setLabelsColor(QColor("#888"));
    jitter_chart_->addAxis(jitter_axis_y_, Qt::AlignLeft);
    jitter_series_->attachAxis(jitter_axis_y_);
}

void MonitoringPanel::setupLossChart() {
    loss_chart_ = new QChart();
    loss_chart_->setTitle("Packet Loss (%)");
    loss_chart_->setTitleBrush(QBrush(QColor("#ffd93d")));
    loss_chart_->setBackgroundBrush(QBrush(QColor("#1a1a1a")));
    loss_chart_->legend()->hide();
    
    loss_series_ = new QLineSeries();
    loss_series_->setPen(QPen(QColor("#ffd93d"), 2));
    loss_chart_->addSeries(loss_series_);
    
    loss_axis_x_ = new QValueAxis();
    loss_axis_x_->setVisible(false);
    loss_chart_->addAxis(loss_axis_x_, Qt::AlignBottom);
    loss_series_->attachAxis(loss_axis_x_);
    
    loss_axis_y_ = new QValueAxis();
    loss_axis_y_->setLabelsColor(QColor("#888"));
    loss_chart_->addAxis(loss_axis_y_, Qt::AlignLeft);
    loss_series_->attachAxis(loss_axis_y_);
}

void MonitoringPanel::updatePingDisplay(double ping_ms) {
    QString color = ping_ms < 50 ? "#4ecdc4" : ping_ms < 100 ? "#ffd93d" : "#ff6b6b";
    ping_value_label_->setText(QString("%1").arg(ping_ms, 0, 'f', 1));
    ping_value_label_->setStyleSheet(QString("font-size: 24px; font-weight: bold; color: %1;").arg(color));
}

void MonitoringPanel::updateJitterDisplay(double jitter_ms) {
    QString color = jitter_ms < 5 ? "#4ecdc4" : jitter_ms < 15 ? "#ffd93d" : "#ff6b6b";
    jitter_value_label_->setText(QString("%1").arg(jitter_ms, 0, 'f', 1));
    jitter_value_label_->setStyleSheet(QString("font-size: 24px; font-weight: bold; color: %1;").arg(color));
}

void MonitoringPanel::updateLossDisplay(double loss_percent) {
    QString color = loss_percent < 1 ? "#4ecdc4" : loss_percent < 3 ? "#ffd93d" : "#ff6b6b";
    loss_value_label_->setText(QString("%1").arg(loss_percent, 0, 'f', 1));
    loss_value_label_->setStyleSheet(QString("font-size: 24px; font-weight: bold; color: %1;").arg(color));
}

void MonitoringPanel::onRefreshStats() {
    if (!target_ip_.empty()) {
        stats_collector_.start("manual_session");
    }
}

void MonitoringPanel::onExportStats() {
    QString filename = QFileDialog::getSaveFileName(this, "Export Stats", "", "CSV Files (*.csv)");
    if (!filename.isEmpty()) {
        stats_collector_.saveSession(filename.toStdString());
    }
}

void MonitoringPanel::updateCharts() {
    ping_chart_->update();
    jitter_chart_->update();
    loss_chart_->update();
}

} // namespace ui
} // namespace gno
