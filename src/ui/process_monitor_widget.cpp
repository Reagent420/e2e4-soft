#include "process_monitor_widget.h"
#include "theme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QGridLayout>
#include <QScrollArea>
#include <QFrame>
#include <QMessageBox>

#include "../core/process_monitor.h"

namespace gno {

ProcessMonitorWidget::ProcessMonitorWidget(QWidget* parent)
    : QWidget(parent)
{
    m_processMonitor = new ProcessMonitor();
    setupUI();

    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, &ProcessMonitorWidget::refreshProcesses);
    m_refreshTimer->start(3000);
    refreshProcesses();
}

void ProcessMonitorWidget::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(16);

    auto* title = new QLabel("Process Monitor", this);
    title->setObjectName("sectionTitle");
    mainLayout->addWidget(title);

    auto* subtitle = new QLabel("Monitor and manage bandwidth-consuming processes", this);
    subtitle->setObjectName("sectionSubtitle");
    mainLayout->addWidget(subtitle);

    auto* btnRow = new QHBoxLayout();
    auto* refreshBtn = new QPushButton("Refresh", this);
    refreshBtn->setObjectName("boostButton");
    refreshBtn->setFixedWidth(120);
    connect(refreshBtn, &QPushButton::clicked, this, &ProcessMonitorWidget::refreshProcesses);
    btnRow->addWidget(refreshBtn);
    btnRow->addStretch();
    mainLayout->addLayout(btnRow);

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    m_processList = new QWidget();
    m_processList->setLayout(new QVBoxLayout(m_processList));
    m_processList->layout()->setContentsMargins(0, 0, 0, 0);
    m_processList->layout()->setSpacing(4);
    scrollArea->setWidget(m_processList);

    mainLayout->addWidget(scrollArea);
}

void ProcessMonitorWidget::refreshProcesses()
{
    auto procs = m_processMonitor->getTopProcesses(30);

    auto* layout = qobject_cast<QVBoxLayout*>(m_processList->layout());
    if (!layout) return;

    QLayoutItem* item;
    while ((item = layout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    for (const auto& proc : procs) {
        auto* card = new QWidget(m_processList);
        card->setObjectName("gameCard");
        card->setFixedHeight(52);

        auto* cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(12, 8, 12, 8);
        cardLayout->setSpacing(12);

        auto* nameLbl = new QLabel(QString::fromStdString(proc.name), card);
        nameLbl->setObjectName("gameTitle");
        nameLbl->setFixedWidth(200);
        cardLayout->addWidget(nameLbl);

        auto* memLbl = new QLabel(QString("%1 MB").arg(proc.memory_mb), card);
        memLbl->setObjectName("sectionSubtitle");
        memLbl->setFixedWidth(80);
        cardLayout->addWidget(memLbl);

        auto* pidLbl = new QLabel(QString("PID: %1").arg(proc.pid), card);
        pidLbl->setObjectName("sectionSubtitle");
        pidLbl->setFixedWidth(80);
        cardLayout->addWidget(pidLbl);

        if (proc.is_game) {
            auto* gameTag = new QLabel("GAME", card);
            gameTag->setObjectName("gameCategory");
            cardLayout->addWidget(gameTag);
        }

        cardLayout->addStretch();

        auto* blockBtn = new QPushButton("Block", card);
        blockBtn->setFixedWidth(70);
        blockBtn->setObjectName("sidebarButton");
        QString pname = QString::fromStdString(proc.name);
        connect(blockBtn, &QPushButton::clicked, this, [this, pname]() {
            m_processMonitor->blockProcess(pname.toStdString());
        });
        cardLayout->addWidget(blockBtn);

        auto* killBtn = new QPushButton("Kill", card);
        killBtn->setFixedWidth(60);
        killBtn->setObjectName("sidebarButton");
        uint32_t pid = proc.pid;
        connect(killBtn, &QPushButton::clicked, this, [this, pid, pname]() {
            onKillClicked(pid, pname);
        });
        cardLayout->addWidget(killBtn);

        layout->addWidget(card);
    }

    layout->addStretch();
}

void ProcessMonitorWidget::onKillClicked(uint32_t pid, const QString& name)
{
    if (m_processMonitor->killProcess(pid)) {
        refreshProcesses();
    }
}

void ProcessMonitorWidget::onBlockClicked(const QString& name)
{
    m_processMonitor->blockProcess(name.toStdString());
}

} // namespace gno
