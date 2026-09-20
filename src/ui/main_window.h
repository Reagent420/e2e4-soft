#pragma once

#include <QMainWindow>
#include <QStackedWidget>
#include <QPointer>
#include <functional>
#include <vector>
#include <QStatusBar>
#include <QLabel>
#include <QCloseEvent>

class SettingsPageWidget;

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
    void boostRequested(bool on);   // Dashboard → безопасный движок буста (main_gui)

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onNavigationChanged(int index);
    void openSettings();

private:
    void setupUi();
    void setupPages();
    QWidget* ensurePage(int index);

    Sidebar* m_sidebar = nullptr;
    QStackedWidget* m_stackedWidget = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_pingLabel = nullptr;
    QLabel* m_jitterLabel = nullptr;
    QLabel* m_lossLabel = nullptr;
    QLabel* m_boostLabel = nullptr;
    QPointer<SettingsPageWidget> m_settingsWidget;
    std::vector<std::function<QWidget*()>> m_page_creators_;
    std::vector<bool> m_page_created_;
};

} // namespace gno
