#include "main_window.h"
#include "sidebar.h"
#include "theme.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QWidget>

#include "dashboard.h"
#include "game_list.h"
#include "game_profiles_widget.h"
#include "monitoring.h"
#include "optimizer.h"
#include "network_tools.h"
#include "process_monitor_widget.h"
#include "session_history_widget.h"
#include "diagnostics_widget.h"
#include "geo_map.h"
#include "settings_page.h"

namespace gno {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setupUi();
    setupPages();

    connect(m_sidebar, &Sidebar::navigationChanged,
            this, &MainWindow::onNavigationChanged);

    m_sidebar->setNavigationIndex(0);
}

void MainWindow::setupUi()
{
    setWindowTitle(theme::APP_NAME);
    setMinimumSize(900, 600);
    resize(1200, 750);
    setObjectName("GNO");

    auto* centralWidget = new QWidget(this);
    auto* mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_sidebar = new Sidebar(this);
    mainLayout->addWidget(m_sidebar);

    m_stackedWidget = new QStackedWidget(this);
    m_stackedWidget->setObjectName("mainStack");
    mainLayout->addWidget(m_stackedWidget);

    setCentralWidget(centralWidget);

    auto* statusBarWidget = new QWidget(this);
    statusBarWidget->setObjectName("statusBar");
    auto* statusBarLayout = new QHBoxLayout(statusBarWidget);
    statusBarLayout->setContentsMargins(16, 8, 16, 8);

    m_statusLabel = new QLabel(QString::fromUtf8("Готово к работе"), statusBarWidget);
    m_statusLabel->setObjectName("statusDisconnected");
    statusBarLayout->addWidget(m_statusLabel);
    statusBarLayout->addStretch();

    auto metricStyle = QString("color: rgba(255,255,255,0.55); font-size: 11px; background: transparent;");

    m_pingLabel = new QLabel(QString::fromUtf8("Пинг: --"), statusBarWidget);
    m_pingLabel->setStyleSheet(metricStyle);
    statusBarLayout->addWidget(m_pingLabel);
    statusBarLayout->addSpacing(12);

    m_jitterLabel = new QLabel(QString::fromUtf8("Джиттер: --"), statusBarWidget);
    m_jitterLabel->setStyleSheet(metricStyle);
    statusBarLayout->addWidget(m_jitterLabel);
    statusBarLayout->addSpacing(12);

    m_lossLabel = new QLabel(QString::fromUtf8("Потери: --"), statusBarWidget);
    m_lossLabel->setStyleSheet(metricStyle);
    statusBarLayout->addWidget(m_lossLabel);
    statusBarLayout->addSpacing(12);

    m_boostLabel = new QLabel(QString::fromUtf8("ОПТИМИЗАЦИЯ: ВЫКЛ"), statusBarWidget);
    m_boostLabel->setObjectName("boostTag");
    statusBarLayout->addWidget(m_boostLabel);
    statusBarLayout->addSpacing(8);

    auto* hint = new QLabel(QString::fromUtf8("E2E4 Soft — оптимизация игровой сети"), statusBarWidget);
    hint->setStyleSheet("color: rgba(255,255,255,0.35); font-size: 11px; background: transparent;");
    statusBarLayout->addWidget(hint);

    setStatusBar(new QStatusBar(this));
    statusBar()->setStyleSheet(
        QString("QStatusBar { background-color: %1; border-top: 1px solid %2; }")
            .arg(theme::Colors::BG_SURFACE, theme::Colors::BORDER));
    statusBar()->addWidget(statusBarWidget, 1);
}

void MainWindow::setupPages()
{
    m_stackedWidget->addWidget(new DashboardWidget(this));
    m_stackedWidget->addWidget(new GameListWidget(this));
    m_stackedWidget->addWidget(new GameProfilesWidget(this));
    m_stackedWidget->addWidget(new MonitoringWidget(this));
    m_stackedWidget->addWidget(new OptimizerWidget(this));
    m_stackedWidget->addWidget(new NetworkToolsWidget(this));
    m_stackedWidget->addWidget(new ProcessMonitorWidget(this));
    m_stackedWidget->addWidget(new SessionHistoryWidget(this));
    m_stackedWidget->addWidget(new DiagnosticsWidget(this));
    m_stackedWidget->addWidget(new GeoMapWidget(this));

    auto* settings = new SettingsPageWidget(this);
    connect(settings, &SettingsPageWidget::themeChanged, this, &MainWindow::themeChanged);
    connect(settings, &SettingsPageWidget::overlayChanged,
            this, &MainWindow::overlaySettingsChanged);
    connect(settings, &SettingsPageWidget::soundChanged,
            this, &MainWindow::soundSettingsChanged);
    connect(settings, &SettingsPageWidget::notificationsChanged,
            this, &MainWindow::notificationsSettingsChanged);
    m_stackedWidget->addWidget(settings);
}

void MainWindow::onNavigationChanged(int index)
{
    m_stackedWidget->setCurrentIndex(index);
}

void MainWindow::updateLiveMetrics(int pingMs, int jitterMs, double lossPercent)
{
    m_pingLabel->setText(QString::fromUtf8("Пинг: %1 мс").arg(pingMs));
    m_jitterLabel->setText(QString::fromUtf8("Джиттер: %1 мс").arg(jitterMs));
    m_lossLabel->setText(QString::fromUtf8("Потери: %1%").arg(lossPercent, 0, 'f', 1));
    m_statusLabel->setText(QString::fromUtf8("Онлайн"));
    m_statusLabel->setObjectName("statusConnected");
    m_statusLabel->style()->unpolish(m_statusLabel);
    m_statusLabel->style()->polish(m_statusLabel);
}

void MainWindow::setBoostIndicator(bool on)
{
    m_boostLabel->setText(on ? QString::fromUtf8("ОПТИМИЗАЦИЯ: ВКЛ") : QString::fromUtf8("ОПТИМИЗАЦИЯ: ВЫКЛ"));
}

void MainWindow::showRecommendation(const QString& text)
{
    if (!text.isEmpty())
        m_statusLabel->setText(text);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    hide();
    event->ignore();
}

void MainWindow::forceShow()
{
    show();
    raise();
    activateWindow();
}

} // namespace gno
