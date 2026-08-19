#pragma once

#include <QWidget>
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

    SpeedTest* m_speedTest;
    DNSManager* m_dnsManager;

    QLabel* m_speedResultLabel;
    QLabel* m_dnsResultLabel;
    QWidget* m_serverGrid;
    QWidget* m_dnsGrid;
    QTimer* m_pollTimer;
};

} // namespace gno
