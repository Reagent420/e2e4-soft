#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLabel>
#include <QTimer>

namespace gno {

class SpeedTest;
class DNSManager;

class NetworkToolsWidget : public QWidget {
    Q_OBJECT

public:
    explicit NetworkToolsWidget(QWidget* parent = nullptr);

private slots:
    void runSpeedTest();
    void onSpeedTestResult();
    void runDNSBenchmark();
    void onDNSResult();
    void applyDNS(const QString& primary, const QString& secondary);

private:
    void setupUI();
    void addUtilitySections(class QVBoxLayout* layout);
    static QString resolveToIp(const QString& host);

    QLineEdit* m_target_ = nullptr;
    QLineEdit* m_dnsHosts_ = nullptr;
    QTextEdit* m_mtr_ = nullptr;
    QTextEdit* m_wifi_ = nullptr;
    QTextEdit* m_conns_ = nullptr;
    QTextEdit* m_mtu_ = nullptr;
    QTextEdit* m_nat_ = nullptr;
    QTextEdit* m_ports_ = nullptr;
    QTextEdit* m_dnsCmp_ = nullptr;
    QTextEdit* m_bloat_ = nullptr;

    SpeedTest* m_speedTest;
    DNSManager* m_dnsManager;

    QLabel* m_speedResultLabel;
    QLabel* m_dnsResultLabel;
    QWidget* m_serverGrid;
    QWidget* m_dnsGrid;
    QTimer* m_pollTimer;
};

} // namespace gno
