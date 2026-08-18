#include "main_window.h"
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QStatusBar>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFrame>
#include <QMessageBox>
#include <QApplication>
#include <QCloseEvent>

namespace gno {
namespace ui {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle("Game Network Optimizer");
    setMinimumSize(1000, 700);
}

MainWindow::~MainWindow() = default;

void MainWindow::initialize() {
    setupUI();
    setupMenuBar();
    setupSystemTray();
    
    game_detector_.scanInstalledGames();
    game_detector_.detectRunningGames();
    
    auto games = game_detector_.getSupportedGames();
    game_selector_->loadGames(games);
    
    status_timer_ = new QTimer(this);
    connect(status_timer_, &QTimer::timeout, this, &MainWindow::updateStatus);
    status_timer_->start(1000);
    
    statusBar()->showMessage("Ready");
}

void MainWindow::setupUI() {
    QWidget* central = new QWidget(this);
    setCentralWidget(central);
    
    QHBoxLayout* main_layout = new QHBoxLayout(central);
    main_layout->setSpacing(0);
    main_layout->setContentsMargins(0, 0, 0, 0);
    
    QFrame* sidebar = new QFrame();
    sidebar->setFixedWidth(300);
    sidebar->setStyleSheet("QFrame { background-color: #2a2a2a; border-right: 1px solid #555; }");
    
    QVBoxLayout* sidebar_layout = new QVBoxLayout(sidebar);
    sidebar_layout->setContentsMargins(16, 16, 16, 16);
    sidebar_layout->setSpacing(12);
    
    QLabel* logo = new QLabel("GNO");
    logo->setStyleSheet("font-size: 28px; font-weight: bold; color: #2a82da; padding: 8px;");
    logo->setAlignment(Qt::AlignCenter);
    sidebar_layout->addWidget(logo);
    
    QLabel* subtitle = new QLabel("Game Network Optimizer");
    subtitle->setStyleSheet("font-size: 11px; color: #888; padding-bottom: 12px;");
    subtitle->setAlignment(Qt::AlignCenter);
    sidebar_layout->addWidget(subtitle);
    
    QFrame* separator1 = new QFrame();
    separator1->setFrameShape(QFrame::HLine);
    separator1->setStyleSheet("color: #555;");
    sidebar_layout->addWidget(separator1);
    
    game_selector_ = new GameSelector();
    sidebar_layout->addWidget(game_selector_);
    
    QFrame* separator2 = new QFrame();
    separator2->setFrameShape(QFrame::HLine);
    separator2->setStyleSheet("color: #555;");
    sidebar_layout->addWidget(separator2);
    
    status_label_ = new QLabel("Status: Not Connected");
    status_label_->setStyleSheet("color: #aaa; font-size: 12px;");
    sidebar_layout->addWidget(status_label_);
    
    QHBoxLayout* stats_layout = new QHBoxLayout();
    ping_label_ = new QLabel("Ping: --");
    ping_label_->setStyleSheet("color: #4ecdc4; font-size: 11px;");
    stats_layout->addWidget(ping_label_);
    
    jitter_label_ = new QLabel("Jitter: --");
    jitter_label_->setStyleSheet("color: #ff6b6b; font-size: 11px;");
    stats_layout->addWidget(jitter_label_);
    
    loss_label_ = new QLabel("Loss: --");
    loss_label_->setStyleSheet("color: #ffd93d; font-size: 11px;");
    stats_layout->addWidget(loss_label_);
    
    sidebar_layout->addLayout(stats_layout);
    
    optimize_button_ = new QPushButton("OPTIMIZE");
    optimize_button_->setStyleSheet(
        "QPushButton { background-color: #2a82da; font-size: 14px; padding: 12px; }"
        "QPushButton:hover { background-color: #3498db; }"
    );
    connect(optimize_button_, &QPushButton::clicked, this, &MainWindow::onOptimizeClicked);
    sidebar_layout->addWidget(optimize_button_);
    
    revert_button_ = new QPushButton("REVERT");
    revert_button_->setStyleSheet(
        "QPushButton { background-color: #555; font-size: 12px; padding: 8px; }"
        "QPushButton:hover { background-color: #666; }"
    );
    revert_button_->setEnabled(false);
    connect(revert_button_, &QPushButton::clicked, this, &MainWindow::onRevertClicked);
    sidebar_layout->addWidget(revert_button_);
    
    sidebar_layout->addStretch();
    
    main_layout->addWidget(sidebar);
    
    stacked_widget_ = new QStackedWidget();
    
    monitoring_panel_ = new MonitoringPanel();
    stacked_widget_->addWidget(monitoring_panel_);
    
    main_layout->addWidget(stacked_widget_);
    
    connect(game_selector_, &GameSelector::gameSelected,
            this, &MainWindow::onGameSelected);
}

void MainWindow::setupMenuBar() {
    QMenuBar* menu_bar = menuBar();
    
    QMenu* file_menu = menu_bar->addMenu("&File");
    file_menu->addAction("&Settings", this, &MainWindow::onSettingsClicked, QKeySequence("Ctrl+,"));
    file_menu->addSeparator();
    file_menu->addAction("&Exit", qApp, &QApplication::quit, QKeySequence("Ctrl+Q"));
    
    QMenu* help_menu = menu_bar->addMenu("&Help");
    help_menu->addAction("&About", this, &MainWindow::onAboutClicked);
}

void MainWindow::setupSystemTray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;
    
    tray_icon_ = new QSystemTrayIcon(this);
    QMenu* tray_menu = new QMenu(this);
    tray_menu->addAction("Show", this, &QWidget::show);
    tray_menu->addAction("Optimize", this, &MainWindow::onOptimizeClicked);
    tray_menu->addAction("Exit", qApp, &QApplication::quit);
    
    tray_icon_->setContextMenu(tray_menu);
    connect(tray_icon_, &QSystemTrayIcon::activated,
            this, &MainWindow::onTrayActivated);
}

void MainWindow::onGameSelected(const GameInfo& game) {
    current_game_ = game;
    status_label_->setText(QString("Game: %1").arg(QString::fromStdString(game.name)));
    
    if (game.is_running) {
        status_label_->setText(QString("Game: %1 (Running)").arg(QString::fromStdString(game.name)));
        optimize_button_->setEnabled(true);
    } else if (game.is_installed) {
        status_label_->setText(QString("Game: %1 (Installed)").arg(QString::fromStdString(game.name)));
        optimize_button_->setEnabled(true);
    } else {
        status_label_->setText(QString("Game: %1 (Not Installed)").arg(QString::fromStdString(game.name)));
        optimize_button_->setEnabled(false);
    }
}

void MainWindow::onOptimizeClicked() {
    if (current_game_.name.empty()) {
        QMessageBox::warning(this, "Warning", "Please select a game first.");
        return;
    }
    
    statusBar()->showMessage("Optimizing network...");
    
    MultipathConfig config;
    config.auto_switch = true;
    config.max_paths = 5;
    multipath_engine_.setConfig(config);
    
    if (!current_game_.server_ips.empty()) {
        multipath_engine_.start(current_game_.server_ips[0]);
    }
    
    FPSBoostConfig fps_config;
    fps_config.disable_game_dvr = true;
    fps_config.disable_fullscreen_optimizations = true;
    fps_config.optimize_power_plan = true;
    fps_optimizer_.applyConfig(fps_config);
    
    is_optimized_ = true;
    optimize_button_->setEnabled(false);
    revert_button_->setEnabled(true);
    updateConnectionStatus(true);
    
    statusBar()->showMessage("Optimization applied successfully!");
    showNotification("Optimization", "Network optimization applied for " + QString::fromStdString(current_game_.name));
}

void MainWindow::onRevertClicked() {
    multipath_engine_.stop();
    fps_optimizer_.revertAll();
    
    is_optimized_ = false;
    optimize_button_->setEnabled(true);
    revert_button_->setEnabled(false);
    updateConnectionStatus(false);
    
    statusBar()->showMessage("All optimizations reverted.");
}

void MainWindow::onSettingsClicked() {
    if (!settings_dialog_) {
        settings_dialog_ = new SettingsDialog(this);
    }
    settings_dialog_->exec();
}

void MainWindow::onAboutClicked() {
    QMessageBox::about(this, "About GNO",
        "Game Network Optimizer v1.0.0\n\n"
        "Professional game network optimization tool.\n"
        "Features: Multipath routing, FPS Boost, Network monitoring.\n\n"
        "Copyright 2024");
}

void MainWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::DoubleClick) {
        show();
        raise();
        activateWindow();
    }
}

void MainWindow::updateStatus() {
    if (multipath_engine_.isActive()) {
        auto metrics = multipath_engine_.getPathMetrics();
        if (!metrics.empty()) {
            double ping = metrics[0].latency_ms;
            double jitter = metrics[0].jitter_ms;
            double loss = metrics[0].packet_loss_percent;
            
            ping_label_->setText(QString("Ping: %1ms").arg(ping, 0, 'f', 1));
            jitter_label_->setText(QString("Jitter: %1ms").arg(jitter, 0, 'f', 1));
            loss_label_->setText(QString("Loss: %1%").arg(loss, 0, 'f', 1));
            
            monitoring_panel_->updateStats(
                PingStats{},
                PacketLossResult{},
                JitterStats{}
            );
        }
    }
}

void MainWindow::onRouteSwitch(uint32_t old_path, uint32_t new_path) {
    showNotification("Route Switch",
        QString("Switched from path %1 to path %2").arg(old_path).arg(new_path));
}

void MainWindow::updateConnectionStatus(bool connected) {
    if (connected) {
        status_label_->setText(QString("Status: Connected to %1").arg(
            QString::fromStdString(current_game_.name)));
        status_label_->setStyleSheet("color: #4ecdc4; font-size: 12px;");
    } else {
        status_label_->setText("Status: Not Connected");
        status_label_->setStyleSheet("color: #aaa; font-size: 12px;");
        ping_label_->setText("Ping: --");
        jitter_label_->setText("Jitter: --");
        loss_label_->setText("Loss: --");
    }
}

void MainWindow::showNotification(const QString& title, const QString& message) {
    if (tray_icon_) {
        tray_icon_->showMessage(title, message);
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (tray_icon_ && tray_icon_->isVisible()) {
        hide();
        event->ignore();
    } else {
        event->accept();
    }
}

} // namespace ui
} // namespace gno
