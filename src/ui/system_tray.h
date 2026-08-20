#pragma once

#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QObject>
#include <QTimer>

namespace gno {

class SystemTray : public QObject {
    Q_OBJECT

public:
    explicit SystemTray(QObject* parent = nullptr);
    ~SystemTray() override = default;

    void updatePing(int pingMs);
    void updateJitter(int jitterMs);
    void updatePacketLoss(double lossPercent);
    void setConnected(bool connected);

    bool isVisible() const { return m_trayIcon->isVisible(); }

signals:
    void showRequested();
    void quitRequested();

private slots:
    void onActivated(QSystemTrayIcon::ActivationReason reason);

private:
    void buildMenu();
    void updateIcon();

    QSystemTrayIcon* m_trayIcon;
    QMenu* m_menu;
    QAction* m_showAction;
    QAction* m_quitAction;

    int m_ping = 0;
    int m_jitter = 0;
    double m_packetLoss = 0.0;
    bool m_connected = false;
};

} // namespace gno
