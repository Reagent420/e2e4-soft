#pragma once

#include <QMainWindow>
#include <QStackedWidget>
#include <QStatusBar>
#include <QLabel>
#include <QCloseEvent>

namespace gno {

class Sidebar;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override = default;

    void forceShow();

    void updateLiveMetrics(int pingMs, int jitterMs, double lossPercent);
    void setBoostIndicator(bool on);
    void showRecommendation(const QString& text);

signals:
    void themeChanged(bool dark);
    void overlaySettingsChanged(bool enabled, int corner, int opacity);
    void soundSettingsChanged(bool enabled);
    void notificationsSettingsChanged(bool enabled);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onNavigationChanged(int index);

private:
    void setupUi();
    void setupPages();

    Sidebar* m_sidebar;
    QStackedWidget* m_stackedWidget;
    QLabel* m_statusLabel;
    QLabel* m_pingLabel;
    QLabel* m_jitterLabel;
    QLabel* m_lossLabel;
    QLabel* m_boostLabel;
};

} // namespace gno
