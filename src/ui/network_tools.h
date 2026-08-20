#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QTimer>

#include <atomic>
#include <thread>

namespace gno {

class SpeedTest;
class DNSManager;

class NetworkToolsWidget : public QWidget {
    Q_OBJECT

public:
    explicit NetworkToolsWidget(QWidget* parent = nullptr);
    ~NetworkToolsWidget() override;

private slots:
    void runSpeedTest();
    void onSpeedTestResult();
    void runDNSBenchmark();

private:
    void setupUI();
    void stopDnsWorker();

    SpeedTest* m_speedTest;
    DNSManager* m_dnsManager;

    QLabel* m_speedResultLabel;
    QLabel* m_dnsResultLabel;
    QWidget* m_serverGrid;
    QWidget* m_dnsGrid;
    QTimer* m_pollTimer;
    std::thread m_dnsWorker;
    std::atomic<bool> m_stopping{false};
    std::atomic<bool> m_dnsRunning{false};
};

} // namespace gno
