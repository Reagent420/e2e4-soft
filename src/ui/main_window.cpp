#include "main_window.h"
#include "sidebar.h"
#include "theme.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QWidget>

#include "dashboard.h"
#include "game_list.h"
#include "monitoring.h"
#include "optimizer.h"
#include "settings_page.h"

namespace gno {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    qApp->setStyleSheet(theme::globalStyleSheet());
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

    m_statusLabel = new QLabel("Disconnected", statusBarWidget);
    m_statusLabel->setObjectName("statusDisconnected");
    statusBarLayout->addWidget(m_statusLabel);
    statusBarLayout->addStretch();

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
    m_stackedWidget->addWidget(new OptimizerWidget(this));
    m_stackedWidget->addWidget(new SettingsPageWidget(this));
}

void MainWindow::onNavigationChanged(int index)
{
    m_stackedWidget->setCurrentIndex(index);
}

void MainWindow::updateConnectionStatus(const QString& status)
{
    bool connected = (status != "Disconnected");
    m_statusLabel->setText(status);
    m_statusLabel->setObjectName(connected ? "statusConnected" : "statusDisconnected");
    m_statusLabel->style()->unpolish(m_statusLabel);
    m_statusLabel->style()->polish(m_statusLabel);
}

} // namespace gno
