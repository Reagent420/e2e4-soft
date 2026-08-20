#pragma once

#include <QWidget>
#include <QVector>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QFont>
#include <QResizeEvent>

#include "../monitoring/ping_monitor.h"

static const int kMaxDataPoints = 60;

class PingChartWidget : public QWidget {
    Q_OBJECT
public:
    explicit PingChartWidget(QWidget* parent = nullptr);
    void addPoint(double value);
    QVector<double> data() const { return data_; }
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    QVector<double> data_;
    void drawGrid(QPainter& p, const QRect& chartRect);
    void drawChart(QPainter& p, const QRect& chartRect);
};

class JitterChartWidget : public QWidget {
    Q_OBJECT
public:
    explicit JitterChartWidget(QWidget* parent = nullptr);
    void addPoint(double value);
    QVector<double> data() const { return data_; }
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    QVector<double> data_;
    void drawGrid(QPainter& p, const QRect& chartRect);
    void drawChart(QPainter& p, const QRect& chartRect);
};

class PacketLossChartWidget : public QWidget {
    Q_OBJECT
public:
    explicit PacketLossChartWidget(QWidget* parent = nullptr);
    void addPoint(double value);
    QVector<double> data() const { return data_; }
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    QVector<double> data_;
    void drawGrid(QPainter& p, const QRect& chartRect);
    void drawChart(QPainter& p, const QRect& chartRect);
};

class BandwidthChartWidget : public QWidget {
    Q_OBJECT
public:
    explicit BandwidthChartWidget(QWidget* parent = nullptr);
    void addDownloadPoint(double value);
    void addUploadPoint(double value);
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    QVector<double> download_data_;
    QVector<double> upload_data_;
    void drawGrid(QPainter& p, const QRect& chartRect);
    void drawChart(QPainter& p, const QRect& chartRect);
};

namespace gno {

class MonitoringWidget : public QWidget {
    Q_OBJECT
public:
    explicit MonitoringWidget(QWidget* parent = nullptr);

private slots:
    void onPingUpdated(double ms);
    void onJitterUpdated(double ms);
    void onLossUpdated(double percent);
    void onGameStarted(const QString& game);
    void onGameEnded(const QString& game);
    void onExportClicked();

private:
    void addLogEntry(const QString& message, const QColor& color = QColor("#94A3B8"));

    PingChartWidget* ping_chart_;
    JitterChartWidget* jitter_chart_;
    PacketLossChartWidget* loss_chart_;
    BandwidthChartWidget* bw_chart_;
    QVBoxLayout* log_layout_;
    QScrollArea* log_scroll_;
    QWidget* log_container_;
    int log_count_ = 0;
    bool last_ok_ = false;
};

} // namespace gno