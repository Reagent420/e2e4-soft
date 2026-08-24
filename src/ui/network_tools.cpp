#include "network_tools.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#undef ERROR
#include "core/net_utils.h"
#include "core/platform_netscan.h"
#include "core/speed_test.h"
#include "core/route_analyzer.h"
#include "remediation/target_discovery.h"
#include <QApplication>
#include <QProcess>
#include <QRegularExpression>
#include <thread>
#include <random>
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

    addUtilitySections(layout);
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

// ===================== v1.7.0 extended utilities =====================

namespace {

using netutils::HopStats;
using namespace netscan;
using namespace remediation;

QString QS(const char* esc) { return QString::fromUtf8(esc); }
std::string US(const char* esc) { return QString::fromUtf8(esc).toStdString(); }
QString U8(const char* esc) { return QS(esc); }

void runAsync(QWidget* ctx, QTextEdit* out, QPushButton* btn,
              const std::function<std::string()>& work) {
    if (btn) btn->setEnabled(false);
    std::thread([ctx, out, btn, work]() {
        std::string r;
        try { r = work(); } catch (const std::exception& e) { r = e.what(); }
        QMetaObject::invokeMethod(ctx, [out, btn, r]() {
            if (out) out->setPlainText(QString::fromUtf8(r.c_str()));
            if (btn) btn->setEnabled(true);
        }, Qt::QueuedConnection);
    }).detach();
}

QTextEdit* makeOut(QWidget* parent) {
    auto* e = new QTextEdit(parent);
    e->setReadOnly(true);
    e->setMaximumHeight(190);
    e->setFontFamily(QStringLiteral("Consolas"));
    e->setStyleSheet(QString("QTextEdit { background-color: %1; color: %2;"
                             " border: 1px solid %7; border-radius: 8px; padding: 8px; font-size: 12px; }")
                         .arg(theme::Colors::BG_SURFACE, theme::Colors::TEXT_PRIMARY,
                              theme::Colors::BORDER));
    return e;
}

QPushButton* makeBtn(const QString& text, QWidget* parent) {
    auto* b = new QPushButton(text, parent);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

std::vector<double> twoProbeJitter(SpeedTest& st, const std::string& ip) {
    std::vector<double> v;
    for (int i = 0; i < 2; ++i) {
        auto res = st.benchmarkServer(ip, 2);
        v.push_back(res.success ? res.latency_ms : -1.0);
    }
    return v;
}

} // namespace

QString NetworkToolsWidget::resolveToIp(const QString& host) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.toStdString().c_str(), nullptr, &hints, &res) != 0 || !res)
        return host; // maybe already an IP
    char buf[64] = {};
    sockaddr_in* sa = reinterpret_cast<sockaddr_in*>(res->ai_addr);
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u", sa->sin_addr.S_un.S_addr & 0xFF,
             (sa->sin_addr.S_un.S_addr >> 8) & 0xFF,
             (sa->sin_addr.S_un.S_addr >> 16) & 0xFF,
             (sa->sin_addr.S_un.S_addr >> 24) & 0xFF);
    freeaddrinfo(res);
    return buf;
}

void NetworkToolsWidget::addUtilitySections(QVBoxLayout* layout)
{
    // ---- target row -----------------------------------------------------
    auto* targetRow = new QHBoxLayout();
    auto* targetLbl = new QLabel(U8("\xD0\xA6\xD0\xB5\xD0\xBB\xD1\x8C\x20\x28IP\x2F\xD1\x85\xD0\xBE\xD1\x81\xD1\x82\x29"), this);
    m_target_ = new QLineEdit(this);
    m_target_->setPlaceholderText(QStringLiteral("1.1.1.1"));
    m_dnsHosts_ = new QLineEdit(this);
    m_dnsHosts_->setPlaceholderText(
        U8("\xD0\xBF\xD1\x80\xD0\xBE\xD0\xB4\x2D\xD1\x85\xD0\xBE\xD1\x81\xD1\x82\x2E\xD0\xB8\xD0%B3\xD1%80\xD1%8B\x2E\xD1%80%D1%83\x2C steamcommunity.com"));
    auto* dnsHostsLbl = new QLabel(U8("\xD0\x94\xD0\xBE\xD0\xBC\xD0\xB5\xD0\xBD\xD1\x8B\x20\xD0\xB4\xD0\xBB\xD1\x8F\x20DNS"), this);

    layout->addWidget(targetLbl);
    layout->addWidget(m_target_);
    layout->addWidget(dnsHostsLbl);
    layout->addWidget(m_dnsHosts_);

    SpeedTest seed;
    if (!seed.getServers().empty()) {
        const std::string first = seed.getServers().front().ip;
        m_target_->setText(QString::fromStdString(first));
    }
    m_dnsHosts_->setText(QStringLiteral("steamcommunity.com, epicgames.com"));

    // ---- 1. MTR-lite ------------------------------------------------------
    auto* mtrGroup = new QGroupBox(U8("\xD0\x9F\xD0\xBE\xD1\x82\xD0\xB5\xD1\x80\xD0\xB8\x20\xD0\xBF\xD0\xBE\x20\xD1\x85\xD0\xBE\xD0\xBF\xD0\xB0\xD0\xBC\x20\x28MTR\x29"), this);
    auto* mtrL = new QVBoxLayout(mtrGroup);
    auto* mtrBtn = makeBtn(U8("\xD0\x9F\xD1\x80\xD0\xBE\xD0%B2\xD0%B5\xD1%80\xD0%B8\xD1%82\xD1\x8C\x20\xD0\xBC\xD0%B0%D1%80\xD1%88\xD1%80\xD1%83\xD1%82"), mtrGroup);
    m_mtr_ = makeOut(mtrGroup);
    mtrL->addWidget(mtrBtn); mtrL->addWidget(m_mtr_);
    layout->addWidget(mtrGroup);
    connect(mtrBtn, &QPushButton::clicked, this, [this, mtrBtn]() {
        runAsync(this, m_mtr_, mtrBtn, [this]() -> std::string {
            const QString ip = resolveToIp(m_target_->text().trimmed());
            RouteAnalyzer ra;
            auto hops = ra.traceroute(ip.toStdString(), 12);
            SpeedTest st; // one instance for all hops (was leaking per hop)
            std::ostringstream out;
            out << "hop  ip               ms     status\n";
            for (auto& h : hops) {
                HopStats s; s.hop = static_cast<int>(h.hop_number); s.ip = h.ip_address;
                if (h.reachable && !h.ip_address.empty() && h.ip_address != "*") {
                    auto v = twoProbeJitter(st, h.ip_address);
                    netutils::summarizeProbes(s, v);
                } else {
                    netutils::summarizeProbes(s, {-1.0});
                }
                char line[128];
                snprintf(line, sizeof(line), "%-4d %-15s %-6.1f %s\n", s.hop,
                         s.ip.c_str(), s.avg_ms, netutils::hopVerdict(s));
                out << line;
            }
            return out.str();
        });
    });

    // ---- 2. Wi-Fi analyzer -------------------------------------------------
    auto* wifiGroup = new QGroupBox(U8("Wi-Fi \xD0\xB0\xD0\xBD\xD0\xB0\xD0\xBB\xD0\xB8\xD0\xB7\xD0\xB0\xD1\x82\xD0\xBE\xD1\x80"), this);
    auto* wifiL = new QVBoxLayout(wifiGroup);
    auto* wifiBtn = makeBtn(U8("\xD0\xA1\xD0\xBA\xD0\xB0\xD0\xBD\xD0\xB8\xD1%80\xD0\xBE\xD0%B2\xD0\xB0\xD1\x82\xD1\x8C\x20\xD1\x81\xD0%B5\xD1\x82\xD0%B8"), wifiGroup);
    m_wifi_ = makeOut(wifiGroup);
    wifiL->addWidget(wifiBtn); wifiL->addWidget(m_wifi_);
    layout->addWidget(wifiGroup);
    connect(wifiBtn, &QPushButton::clicked, this, [this, wifiBtn]() {
        runAsync(this, m_wifi_, wifiBtn, []() -> std::string {
            QProcess proc;
            proc.start(QStringLiteral("cmd.exe"),
                       QStringList{"/c", "chcp 65001>nul & netsh wlan show networks mode=bssid"});
            proc.waitForFinished(8000);
            const std::string raw = QString::fromUtf8(proc.readAllStandardOutput()).toStdString();
            auto nets = netutils::parseNetshWlan(raw);
            if (nets.empty())
                return US("\xD0\xA1\xD0\xB5\xD1\x82\xD0\xB8\x20\xD0\xBD\xD0\xB5\x20\xD0\xBD\xD0\xB0\xD0%B9\xD0\xB4\xD0%B5\xD0\xBD\xD1\x8B\x20\x28Wi-Fi\x20\xD0\xBE\xD1\x82\xD0\xBA\xD0\xBB\xD1\x8E\xD1\x87\xD0\xB5\xD0\xBD\x3F\x29");
            std::ostringstream out;
            out << "SSID                 CH   SIG%  BSSID\n";
            int count24 = 0;
            for (auto& n : nets) {
                if (n.channel >= 1 && n.channel <= 13) ++count24;
                char line[160];
                snprintf(line, sizeof(line), "%-20s %-4d %-5d %s\n",
                         n.ssid.substr(0, 20).c_str(), n.channel,
                         netutils::signalQuality(n.rssi), n.bssid.c_str());
                out << line;
            }
            if (count24 >= 2) {
                const int ch = netutils::bestChannel24(nets, -1);
                out << "\n" << US("\xD0\xA1\xD0\xBE\xD0\xB2\xD0\xB5\xD1\x82\x3A\x20\xD0\xBC\xD0\xB5\xD0\xBD\xD0%B5\xD0\xB5\x20\xD0\xB7\xD0%B0%D0%B3\xD1%80\xD1%83\xD0%B6\xD0%B5\xD0\xBD\xD0\xBD\xD1%8B\xD0%B9\x20\xD0\xBA\xD0\xB0\xD0\xBD\xD0\xB0\xD0\xBB\x3A\x20") << ch;
            }
            return out.str();
        });
    });

    // ---- 3. Connections of the running game --------------------------------
    auto* connsGroup = new QGroupBox(U8("\xD0\xA1\xD0\xBE\xD0\xB5\xD0%B4\xD0%B8\xD0\xBD\xD0%B5\xD0\xBD\xD0\xB8\xD1\x8F\x20\xD0%B8%D0%B3%D1%80\xD1%8B"), this);
    auto* connsL = new QVBoxLayout(connsGroup);
    auto* connsBtn = makeBtn(U8("\xD0\x9F\xD0\xBE\xD0\xBA\xD0\xB0\xD0%B7\xD0\xB0\xD1\x82\xD1\x8C\x20\xD0\xB8\x20\xD0\xBF\xD0%B8%D0\xBD\xD0%B3\xD0\xBE\xD0\xBD\xD1%83\xD1%82\xD1\x8C"), connsGroup);
    m_conns_ = makeOut(connsGroup);
    connsL->addWidget(connsBtn); connsL->addWidget(m_conns_);
    layout->addWidget(connsGroup);
    connect(connsBtn, &QPushButton::clicked, this, [this, connsBtn]() {
        runAsync(this, m_conns_, connsBtn, []() -> std::string {
            auto game = discoverRunningGameProcess();
            if (!game)
                return US("\xD0\x98\xD0%B3%D1%80\xD0\xB0\x20\xD0\xBD\xD0\xB5\x20\xD0%B7\xD0\xB0\xD0\xBF\xD1%83\xD1%89\xD0%B5\xD0\xBD\xD0\xB0.");
            auto eps = remoteTcpEndpointsForPid(game->pid);
            std::ostringstream out;
            out << US("\xD0\x9F\xD1%80\xD0\xBE\xD1\x86\xD0%B5\xD1\x81\xD1\x81\x3A\x20") << game->path << "\n\n";
            if (eps.empty()) out << US("\xD0\x9D\xD0\xB5\xD1\x82\x20\xD0\xB0\xD0\xBA\xD1%82\xD0%B8\xD0%B2\xD0\xBD\xD1%8B\xD1\x85\x20TCP-\xD1\x81\xD0\xBE\xD0%B5\xD0%B4\xD0%B8\xD0\xBD\xD0%B5\xD0\xBD\xD0\xB8\xD0%B9.\n");
            int shown = 0;
            SpeedTest st;
            for (const auto& ep : eps) {
                if (++shown > 6) break;
                const auto pos = ep.rfind(':');
                const std::string ip = ep.substr(0, pos);
                out << ep << "  ";
                auto res = st.benchmarkServer(ip, 3);
                out << (res.success ? (std::to_string(static_cast<int>(res.latency_ms)) + " ms") : std::string("-")) << "\n";
            }
            auto listeners = listeningTcpPortsForPid(game->pid);
            if (!listeners.empty()) {
                out << US("\n\xD0\xA1\xD0\xBB\xD1%83\xD1%88\xD0%B0\xD0%B5\xD1\x82\x20\xD0\xBF\xD0\xBE%D1%80\xD1%82\xD1%8B\x3A\x20");
                for (auto p : listeners) out << p << " ";
                out << "\n";
            }
            return out.str();
        });
    });

    // ---- 4. Path MTU (DF probe) --------------------------------------------
    auto* mtuGroup = new QGroupBox(U8("MTU\x20\xD0\xBF\xD1%83\xD1%82\xD0\xB8\x20\x28DF-probe\x29"), this);
    auto* mtuL = new QVBoxLayout(mtuGroup);
    auto* mtuBtn = makeBtn(U8("\xD0\x9E\xD0\xBF\xD1%80\xD0%B5\xD0%B4\xD0%B5\xD0\xBB\xD0%B8\xD1\x82\xD1\x8C\x20MTU"), mtuGroup);
    m_mtu_ = makeOut(mtuGroup);
    mtuL->addWidget(mtuBtn); mtuL->addWidget(m_mtu_);
    layout->addWidget(mtuGroup);
    connect(mtuBtn, &QPushButton::clicked, this, [this, mtuBtn]() {
        runAsync(this, m_mtu_, mtuBtn, [this]() -> std::string {
            const QString ip = resolveToIp(m_target_->text().trimmed());
            int lo = 1000, hi = 1501;
            if (!netscan::icmpDfProbe(ip.toStdString(), lo, 1500).ok) {
                while (lo > 576 && !netscan::icmpDfProbe(ip.toStdString(), lo, 1200).ok) lo -= 100;
                if (lo <= 576) return US("\xD0\xA6\xD0%B5\xD0\xBB\xD1\x8C\x20\xD0\xBD\xD0%B5\x20\xD0\xBE\xD1\x82\xD0%B2\xD0%B5\xD1%87\xD0%B0\xD0%B5\xD1\x82.");
            }
            hi = lo + 500;
            for (int step = 0; step < 9; ++step) {
                const int mid = netutils::nextMtuProbe(lo, hi);
                if (mid == lo) break;
                if (netscan::icmpDfProbe(ip.toStdString(), mid, 1200).ok) lo = mid; else hi = mid;
            }
            return US("\x50\x61\x74\x68\x20\x4D\x54\x55\x20\xE2\x89\x88\x20") + std::to_string(lo + 28) +
                    US("\x20\xD0%B1\xD0\xB0\xD0%B9\xD1%82\x20\x28payload\x20") + std::to_string(lo) + ")";
        });
    });

    // ---- 5. NAT type via STUN ----------------------------------------------
    auto* natGroup = new QGroupBox(U8("NAT-\xD1\x82\xD0\xB8\xD0\xBF\x20\x28STUN\x29"), this);
    auto* natL = new QVBoxLayout(natGroup);
    auto* natBtn = makeBtn(U8("\xD0\x9E\xD0\xBF\xD1%80\xD0%B5\xD0%B4\xD0%B5\xD0\xBB\xD0%B8\xD1\x82\xD1\x8C\x20NAT"), natGroup);
    m_nat_ = makeOut(natGroup);
    natL->addWidget(natBtn); natL->addWidget(m_nat_);
    layout->addWidget(natGroup);
    connect(natBtn, &QPushButton::clicked, this, [this, natBtn]() {
        runAsync(this, m_nat_, natBtn, []() -> std::string {
            std::mt19937 rng(std::random_device{}());
            auto resolveStun = [&](const char* host, std::string& ip) -> bool {
                auto q = netutils::buildDnsQueryA(host, 0x1234);
                std::vector<std::uint8_t> resp;
                if (!dnsQueryUdp("8.8.8.8", q, 2500, resp)) return false;
                auto ips = netutils::parseDnsARecords(resp.data(), resp.size());
                if (ips.empty()) return false;
                ip = ips.front();
                return true;
            };
            std::string ip1, ip2;
            if (!resolveStun("stun.l.google.com", ip1)) return US("\x53\x54\x55\x4E\x20\xD0\xBD\xD0%B5\xD0%B4\xD0%BE\xD1%81\xD1%82\xD1%83\xD0\xBF\xD0%B5\xD0\xBD.");
            resolveStun("stun1.l.google.com", ip2);
            if (ip2.empty()) ip2 = ip1;

            std::uint8_t tid[12];
            for (auto& b : tid) b = static_cast<std::uint8_t>(rng());
            auto req = netutils::buildBindingRequest(tid);
            std::vector<std::uint8_t> r1, r2;
            std::string m1, m2; std::uint16_t p1 = 0, p2 = 0;
            const bool ok1 = stunExchange(ip1, 19302, req, 3000, r1) &&
                             netutils::parseXorMappedAddress(r1.data(), r1.size(), m1, p1);
            for (auto& b : tid) b = static_cast<std::uint8_t>(rng());
            req = netutils::buildBindingRequest(tid);
            const bool ok2 = stunExchange(ip2, 19302, req, 3000, r2) &&
                             netutils::parseXorMappedAddress(r2.data(), r2.size(), m2, p2);
            if (!ok1) return US("\x53\x54\x55\x4E\x20\xD0\xBD\xD0%B5\x20\xD0\xBE\xD1\x82\xD0\xB2\xD0%B5\xD1\x82\xD0\xB8\xD0\xBB.");

            std::ostringstream out;
            out << US("\xD0\x92\xD0\xBD\xD0%B5\xD1\x88\xD0\xBD\xD0%B8\xD0%B9\x20IP\x3A\x20") << m1 << "\n";
            if (ok2) {
                out << US("NAT\x3A\x20") << netutils::classifyNat(m1 == m2) << "\n";
                if (m1 != m2)
                    out << US("\x53\x79\x6D\x6D\x65\x74\x72\x69\x63\x20NAT\x20\xD0\xBC\xD0\xBE%D0%B6\xD0%B5\xD1%82\x20\xD0\xBC%D0%B5\xD1%88\xD0%B0\xD1\x82\xD1\x8C\x20P2P-\xD1\x81\xD0\xBE\xD0%B5\xD0%B4\xD0%B8\xD0\xBD\xD0%B5\xD0\xBD\xD0\xB8\xD1\x8F\xD0\xBC.");
                else
                    out << US("Cone-NAT\x20\xD0\xBE\xD0\xB1\xD1%8B\xD1%87\xD0\xBD\xD0\xBE\x20\xD1%85\xD0\xBE\xD1%80\xD0\xBE\xD1\x88\x20\xD0\xB4\xD0\xBB\xD1\x8F\x20\xD0\xB8%D0%B3\xD1\x80.");
            }
            out << "\nLocal IP: " << localIp();
            return out.str();
        });
    });

    // ---- 6. Ports -----------------------------------------------------------
    auto* portsGroup = new QGroupBox(U8("\xD0\x9F\xD0\xBE%D1%80\xD1%82\xD1%8B\x20\xD0\xB8\x20\xD1\x81\xD0\xBB\xD1%83\xD1%88\xD0%B0\xD1%82\xD0%B5\xD0\xBB\xD0\xB8"), this);
    auto* portsL = new QVBoxLayout(portsGroup);
    auto* portsBtn = makeBtn(U8("\xD0\x9F\xD1%80\xD0\xBE\xD0%B2\xD0%B5\xD1%80\xD0\xB8\xD1\x82\xD1\x8C\x20\xD0\xBF\xD0\xBE%D1%80\xD1%82\xD1%8B"), portsGroup);
    m_ports_ = makeOut(portsGroup);
    portsL->addWidget(portsBtn); portsL->addWidget(m_ports_);
    layout->addWidget(portsGroup);
    connect(portsBtn, &QPushButton::clicked, this, [this, portsBtn]() {
        runAsync(this, m_ports_, portsBtn, [this]() -> std::string {
            const QString ip = resolveToIp(m_target_->text().trimmed());
            static const std::pair<int, const char*> common[] = {
                {443, "HTTPS/CDN"}, {80, "HTTP"}, {3074, "Xbox LIVE"}, {27015, "Source query"}};
            std::ostringstream out;
            out << US("\xD0\x94\xD0\xBE\xD1\x81\xD1%82\xD1%83\xD0\xBF\xD0\xBD\xD0\xBE\xD1\x81\xD1\x82\xD1\x8C\x20TCP\x20\xD0\xBF\xD0\xBE%D1%80\xD1%82\xD0\xBE\xD0\xB2\x3A\x20") << ip.toStdString() << "\n";
            for (auto& pr : common) {
                const bool open = tcpConnectCheck(ip.toStdString(),
                                                  static_cast<std::uint16_t>(pr.first), 2000);
                char line[96];
                snprintf(line, sizeof(line), "  %-5d %-14s %s\n", pr.first, pr.second,
                         open ? "OPEN" : "closed/filtered");
                out << line;
            }
            if (auto game = discoverRunningGameProcess()) {
                auto l = listeningTcpPortsForPid(game->pid);
                if (!l.empty()) {
                    out << US("\n\xD0\x98\xD0%B3%D1%80\xD0\xB0\x20\xD1\x81\xD0\xBB\xD1%83\xD1%88\xD0%B0\xD0%B5\xD1\x82\x3A\x20");
                    for (auto p : l) out << p << " ";
                    out << "\n";
                }
            }
            return out.str();
        });
    });

    // ---- 7. DNS compare -------------------------------------------------------
    auto* dnsCmpGroup = new QGroupBox(U8("DNS\x3A\x20\xD1\x81\xD0\xB8\xD1\x81\xD1%82\xD0%B5\xD0\xBC\xD0\xBD\xD1%8B\xD0\xB9\x20vs\x201.1.1.1\x20vs\x208.8.8.8"), this);
    auto* dnsCmpL = new QVBoxLayout(dnsCmpGroup);
    auto* dnsCmpBtn = makeBtn(U8("\xD0\xA1\xD1%80\xD0\xB0\xD0\xB2\xD0\xBD\xD0\xB8\xD1\x82\xD1\x8C\x20DNS"), dnsCmpGroup);
    m_dnsCmp_ = makeOut(dnsCmpGroup);
    dnsCmpL->addWidget(dnsCmpBtn); dnsCmpL->addWidget(m_dnsCmp_);
    layout->addWidget(dnsCmpGroup);
    connect(dnsCmpBtn, &QPushButton::clicked, this, [this, dnsCmpBtn]() {
        runAsync(this, m_dnsCmp_, dnsCmpBtn, [this]() -> std::string {
            std::ostringstream out;
            out << "host                          system    1.1.1.1   8.8.8.8\n";
            const QStringList hosts = m_dnsHosts_->text().split(',', Qt::SkipEmptyParts);
            for (QString raw : hosts) {
                const std::string host = raw.trimmed().toStdString();
                if (host.empty()) continue;

                double sys_ms = -1;
                systemResolveMs(host, sys_ms);

                double c1 = -1, c8 = -1;
                std::vector<std::uint8_t> a1, a8;
                auto q1 = netutils::buildDnsQueryA(host, 0x4A41);
                auto t0 = std::chrono::steady_clock::now();
                const bool ok1 = dnsQueryUdp("1.1.1.1", q1, 2000, a1);
                auto t1 = std::chrono::steady_clock::now();
                if (ok1) c1 = std::chrono::duration<double, std::milli>(t1 - t0).count();

                auto q8 = netutils::buildDnsQueryA(host, 0x4A42);
                t0 = std::chrono::steady_clock::now();
                const bool ok8 = dnsQueryUdp("8.8.8.8", q8, 2000, a8);
                t1 = std::chrono::steady_clock::now();
                if (ok8) c8 = std::chrono::duration<double, std::milli>(t1 - t0).count();

                auto fmt = [](double v) { return v < 0 ? "-" : std::to_string(static_cast<int>(v)) + "ms"; };
                char line[128];
                snprintf(line, sizeof(line), "%-29s %-9s %-9s %-9s\n", host.substr(0, 28).c_str(),
                         fmt(sys_ms).c_str(), ok1 ? fmt(c1).c_str() : "-", ok8 ? fmt(c8).c_str() : "-");
                out << line;
            }
            return out.str();
        });
    });

    // ---- 8. Bufferbloat ---------------------------------------------------------
    auto* bloatGroup = new QGroupBox(U8("Bufferbloat\x20\x28\xD0\xBF\xD0%B8%D0\xBD\xD0%B3\x20\xD0\xBF\xD0\xBE%D0%B4\x20\xD0\xBD\xD0%B0%D0%B3\xD1%80\xD1%83\xD0%B7\xD0\xBA\xD0\xBE\xD0%B9\x29"), this);
    auto* bloatL = new QVBoxLayout(bloatGroup);
    auto* bloatBtn = makeBtn(U8("\xD0\x97\xD0\xB0\xD0\xBC%D0%B5\xD1%80\xD0\xB8\xD1\x82\xD1\x8C"), bloatGroup);
    m_bloat_ = makeOut(bloatGroup);
    bloatL->addWidget(bloatBtn); bloatL->addWidget(m_bloat_);
    layout->addWidget(bloatGroup);
    connect(bloatBtn, &QPushButton::clicked, this, [this, bloatBtn]() {
        runAsync(this, m_bloat_, bloatBtn, []() -> std::string {
            SpeedTest st;
            auto idleRes = st.benchmarkServer("1.1.1.1", 10);
            if (!idleRes.success)
                return US("\xD0\x9D\xD0%B5\xD1\x82\x20\xD1\x81\xD0%B2\xD1%8F\xD0%B7\xD0\xB8\x20\xD1\x81\x20\x31\x2E\x31\x2E\x31\x2E\x31");
            const double idle = idleRes.latency_ms;

            std::thread loader([] { httpDownloadLoad("cachefly.cachefly.net", "/10mb.test", 6); });
            std::vector<double> under;
            for (int i = 0; i < 10; ++i) {
                auto r = st.benchmarkServer("1.1.1.1", 2);
                if (r.success) under.push_back(r.latency_ms);
            }
            loader.join();
            if (under.empty()) return US("\xD0\x9D\xD0%B5\xD1\x82\x20\xD0\xB4\xD0%B0\xD0\xBD\xD0\xBD\xD1%8B\xD1\x85\x20\xD0\xBF\xD0\xBE%D0%B4\x20\xD0\xBD\xD0%B0%D0%B3\xD1%80\xD1%83\xD0%B7\xD0\xBA\xD0\xBE\xD0%B9.");

            double sum = 0;
            for (double v : under) sum += v;
            const double loaded = sum / under.size();
            const auto g = netutils::gradeBufferbloat(idle, loaded);
            std::ostringstream out;
            out << US("\xD0\x91\xD0\xB5%D0%B7\x20\xD0\xBD\xD0%B0%D0%B3\xD1%80\xD1%83\xD0%B7\xD0\xBA\xD0\xB8\x3A\x20") << static_cast<int>(idle) << " ms\n";
            out << US("\xD0\x9F\xD0\xBE%D0%B4\x20\xD0\xBD\xD0%B0%D0%B3\xD1%80\xD1%83\xD0%B7\xD0\xBA\xD0\xBE\xD0%B9\x3A\x20") << static_cast<int>(loaded) << " ms\n";
            out << "+" << static_cast<int>(g.delta_percent) << "%  [" << g.tag << "]";
            return out.str();
        });
    });
}

} // namespace gno
