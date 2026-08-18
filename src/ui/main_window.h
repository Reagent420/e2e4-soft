#pragma once

#include <QMainWindow>
#include <QStackedWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QSystemTrayIcon>
#include <QTimer>
#include "game_selector.h"
#include "monitoring_panel.h"
#include "settings_dialog.h"
#include "../core/multipath_engine.h"
#include "../core/game_detector.h"
#include "../optimization/fps_optimizer.h"
#include "../monitoring/stats_collector.h"

namespace gno {
namespace ui {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

    void initialize();

private slots:
    void onGameSelected(const GameInfo& game);
    void onOptimizeClicked();
    void onRevertClicked();
    void onSettingsClicked();
    void onAboutClicked();
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void updateStatus();
    void onRouteSwitch(uint32_t old_path, uint32_t new_path);

private:
    void setupUI();
    void setupMenuBar();
    void setupSystemTray();
    void updateConnectionStatus(bool connected);
    void showNotification(const QString& title, const QString& message);

    QStackedWidget* stacked_widget_;
    GameSelector* game_selector_;
    MonitoringPanel* monitoring_panel_;
    SettingsDialog* settings_dialog_;
    
    QLabel* status_label_;
    QLabel* ping_label_;
    QLabel* jitter_label_;
    QLabel* loss_label_;
    QPushButton* optimize_button_;
    QPushButton* revert_button_;
    
    QSystemTrayIcon* tray_icon_;
    QTimer* status_timer_;

    MultipathEngine multipath_engine_;
    GameDetector game_detector_;
    FPSOptimizer fps_optimizer_;
    StatsCollector stats_collector_;
    
    GameInfo current_game_;
    bool is_optimized_ = false;
};

} // namespace ui
} // namespace gno
