#pragma once

#include <QWidget>
#include <QVector>
#include <QTimer>
#include <QLabel>
#include <QPushButton>

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
    void updatePing(double ms);
    void updateJitter(double ms);
    void updatePacketLoss(double percent);
    void updateRouteCount(int count);
    void setConnected(bool connected);

signals:
    void boostToggled(bool active);

private slots:
    void onBoostClicked();
    void onRefresh();

private:
    QTimer* refresh_timer_;
    PingGraphWidget* graph_;
    QLabel* ping_value_;
    QLabel* jitter_value_;
    QLabel* loss_value_;
    QLabel* route_value_;
    QPushButton* boost_btn_;
    bool boosting_ = false;

    QWidget* createMetricCard(const QString& label, QLabel** valueOut, QLabel** deltaOut, const QString& deltaColor);
};
