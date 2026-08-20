#include "main_window.h"
#include "sidebar.h"
#include "theme.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QWidget>

#include "dashboard.h"
#include "game_list.h"
#include "monitoring.h"
#include "network_tools.h"
#include "session_history_widget.h"
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

    auto* hint = new QLabel(QString::fromUtf8("E2E4 Soft — диагностика игровых маршрутов"), statusBarWidget);
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
    m_stackedWidget->addWidget(new MonitoringWidget(this));
    m_stackedWidget->addWidget(new NetworkToolsWidget(this));
    m_stackedWidget->addWidget(new SessionHistoryWidget(this));

    auto* settings = new SettingsPageWidget(this);
    connect(settings, &SettingsPageWidget::themeChanged, this, &MainWindow::themeChanged);
    m_stackedWidget->addWidget(settings);
}

void MainWindow::onNavigationChanged(int index)
{
    m_stackedWidget->setCurrentIndex(index);
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
