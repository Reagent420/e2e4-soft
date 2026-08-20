#include "network_tools.h"
#include "theme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QGridLayout>
#include <QScrollArea>
#include <QFrame>
#include <QGroupBox>

#include "../core/speed_test.h"
#include "../core/dns_manager.h"

namespace gno {

NetworkToolsWidget::NetworkToolsWidget(QWidget* parent)
    : QWidget(parent)
{
    m_speedTest = new SpeedTest();
    m_dnsManager = new DNSManager();
    setupUI();

    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &NetworkToolsWidget::onSpeedTestResult);
}

void NetworkToolsWidget::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(16);

    auto* title = new QLabel(QString::fromUtf8("Сетевые утилиты"), this);
    title->setObjectName("sectionTitle");
    mainLayout->addWidget(title);

    auto* subtitle = new QLabel(QString::fromUtf8("Спидтест, бенчмарк DNS и настройка серверов"), this);
    subtitle->setObjectName("sectionSubtitle");
    mainLayout->addWidget(subtitle);

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto* content = new QWidget();
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    // Speedtest section
    auto* speedGroup = new QGroupBox(QString::fromUtf8("Спидтест"), content);
    auto* speedLayout = new QVBoxLayout(speedGroup);

    m_speedResultLabel = new QLabel(QString::fromUtf8("Замер пинга до серверов по всему миру"), speedGroup);
    m_speedResultLabel->setObjectName("sectionSubtitle");
    speedLayout->addWidget(m_speedResultLabel);

    auto* speedBtn = new QPushButton(QString::fromUtf8("Запустить спидтест"), speedGroup);
    speedBtn->setObjectName("boostButton");
    speedBtn->setFixedWidth(180);
    connect(speedBtn, &QPushButton::clicked, this, &NetworkToolsWidget::runSpeedTest);
    speedLayout->addWidget(speedBtn);

    m_serverGrid = new QWidget(speedGroup);
    m_serverGrid->setLayout(new QGridLayout(m_serverGrid));
    speedLayout->addWidget(m_serverGrid);

    layout->addWidget(speedGroup);

    // DNS section
    auto* dnsGroup = new QGroupBox(QString::fromUtf8("Настройки DNS"), content);
    auto* dnsLayout = new QVBoxLayout(dnsGroup);

    m_dnsResultLabel = new QLabel(QString::fromUtf8("Выберите быстрый DNS-сервер для вашего соединения"), dnsGroup);
    m_dnsResultLabel->setObjectName("sectionSubtitle");
    dnsLayout->addWidget(m_dnsResultLabel);

    auto* dnsBtnRow = new QHBoxLayout();
    auto* dnsBenchBtn = new QPushButton(QString::fromUtf8("Бенчмарк DNS"), dnsGroup);
    dnsBenchBtn->setObjectName("boostButton");
    dnsBenchBtn->setFixedWidth(180);
    connect(dnsBenchBtn, &QPushButton::clicked, this, &NetworkToolsWidget::runDNSBenchmark);
    dnsBtnRow->addWidget(dnsBenchBtn);

    auto* dnsResetBtn = new QPushButton(QString::fromUtf8("Сброс на DHCP"), dnsGroup);
    dnsResetBtn->setObjectName("sidebarButton");
    dnsResetBtn->setFixedWidth(140);
    connect(dnsResetBtn, &QPushButton::clicked, this, [this]() {
        m_dnsManager->resetToDHCP();
        m_dnsResultLabel->setText(QString::fromUtf8("DNS сброшен — настройки автоматические (DHCP)"));
    });
    dnsBtnRow->addWidget(dnsResetBtn);
    dnsBtnRow->addStretch();
    dnsLayout->addLayout(dnsBtnRow);

    m_dnsGrid = new QWidget(dnsGroup);
    m_dnsGrid->setLayout(new QGridLayout(m_dnsGrid));
    dnsLayout->addWidget(m_dnsGrid);

    layout->addWidget(dnsGroup);
    layout->addStretch();

    scrollArea->setWidget(content);
    mainLayout->addWidget(scrollArea);
}

void NetworkToolsWidget::runSpeedTest()
{
    m_speedResultLabel->setText(QString::fromUtf8("Запуск спидтеста…"));
    m_speedTest->runBenchmark();
    m_pollTimer->start(500);

    auto servers = m_speedTest->getServers();
    QGridLayout* grid = qobject_cast<QGridLayout*>(m_serverGrid->layout());
    if (!grid) return;

    QLayoutItem* item;
    while ((item = grid->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    int row = 0;
    for (const auto& srv : servers) {
        auto* nameLbl = new QLabel(QString::fromStdString(srv.name), m_serverGrid);
        nameLbl->setObjectName("gameTitle");
        grid->addWidget(nameLbl, row, 0);

        auto* pingLbl = new QLabel("---", m_serverGrid);
        pingLbl->setObjectName("sectionSubtitle");
        pingLbl->setProperty("server_ip", QString::fromStdString(srv.ip));
        grid->addWidget(pingLbl, row, 1);

        auto* btn = new QPushButton(QString::fromUtf8("Пинг"), m_serverGrid);
        btn->setFixedWidth(80);
        btn->setProperty("server_ip", QString::fromStdString(srv.ip));
        connect(btn, &QPushButton::clicked, this, [this, btn]() {
            std::string ip = btn->property("server_ip").toString().toStdString();
            auto result = m_speedTest->benchmarkServer(ip);
            for (auto* child : m_serverGrid->findChildren<QLabel*>()) {
                if (child->property("server_ip").toString() == QString::fromStdString(ip)) {
                    if (result.success) {
                        child->setText(QString("%1 мс").arg(result.latency_ms, 0, 'f', 1));
                    } else {
                        child->setText(QString::fromUtf8("Тайм-аут"));
                    }
                    break;
                }
            }
        });
        grid->addWidget(btn, row, 2);
        row++;
    }
}

void NetworkToolsWidget::onSpeedTestResult()
{
    if (m_speedTest->isRunning()) return;
    m_pollTimer->stop();

    auto results = m_speedTest->getResults();
    int success = 0;
    double best_ping = 999999;
    std::string best_server;

    for (const auto& r : results) {
        if (r.success) {
            success++;
            if (r.latency_ms < best_ping) {
                best_ping = r.latency_ms;
                best_server = r.server_name;
            }
        }
    }

    if (success > 0) {
        m_speedResultLabel->setText(
            QString("Лучший: %1 (%2 мс) — проверено серверов: %3")
                .arg(QString::fromStdString(best_server))
                .arg(best_ping, 0, 'f', 1)
                .arg(success));
    } else {
        m_speedResultLabel->setText(QString::fromUtf8("Ни один сервер не ответил"));
    }
}

void NetworkToolsWidget::runDNSBenchmark()
{
    m_dnsResultLabel->setText(QString::fromUtf8("Тестируем DNS-серверы…"));

    std::thread([this]() {
        auto results = m_dnsManager->benchmarkAll();
        auto fastest = m_dnsManager->getFastestServer();

        QMetaObject::invokeMethod(this, [this, fastest]() {
            if (fastest.success) {
                m_dnsResultLabel->setText(
                    QString("Быстрый DNS: %1 (%2 мс)")
                        .arg(QString::fromStdString(fastest.server))
                        .arg(fastest.latency_ms, 0, 'f', 1));
            } else {
                m_dnsResultLabel->setText(QString::fromUtf8("Бенчмарк DNS не удался"));
            }

            QGridLayout* grid = qobject_cast<QGridLayout*>(m_dnsGrid->layout());
            if (!grid) return;

            QLayoutItem* item;
            while ((item = grid->takeAt(0)) != nullptr) {
                if (item->widget()) item->widget()->deleteLater();
                delete item;
            }

            auto presets = m_dnsManager->getPresets();
            auto benchResults = m_dnsManager->getResults();

            int row = 0;
            for (size_t i = 0; i < presets.size(); ++i) {
                auto* nameLbl = new QLabel(
                    QString("%1 (%2)").arg(
                        QString::fromStdString(presets[i].name),
                        QString::fromStdString(presets[i].primary)),
                    m_dnsGrid);
                nameLbl->setObjectName("gameTitle");
                grid->addWidget(nameLbl, row, 0);

                std::string latencyText = "---";
                if (i < benchResults.size() && benchResults[i].success) {
                    latencyText = std::to_string((int)benchResults[i].latency_ms) + " ms";
                }
                auto* pingLbl = new QLabel(QString::fromStdString(latencyText), m_dnsGrid);
                pingLbl->setObjectName("sectionSubtitle");
                grid->addWidget(pingLbl, row, 1);

                auto* btn = new QPushButton(QString::fromUtf8("Применить"), m_dnsGrid);
                btn->setFixedWidth(80);
                QString pri = QString::fromStdString(presets[i].primary);
                QString sec = QString::fromStdString(presets[i].secondary);
                connect(btn, &QPushButton::clicked, this, [this, pri, sec]() {
                    applyDNS(pri, sec);
                });
                grid->addWidget(btn, row, 2);
                row++;
            }
        });
    }).detach();
}

void NetworkToolsWidget::onDNSResult()
{
}

void NetworkToolsWidget::applyDNS(const QString& primary, const QString& secondary)
{
    m_dnsManager->applyDNS(primary.toStdString(), secondary.toStdString());
    m_dnsResultLabel->setText(QString("DNS установлен: %1").arg(primary));
}

} // namespace gno
