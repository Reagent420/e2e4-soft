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
    void setAlertThresholds(double maxPing, double maxLoss);
    void updateJitter(int jitterMs);
    void updatePacketLoss(double lossPercent);
    void setConnected(bool connected);
    void setBoostOn(bool on);
    void setOverlayOn(bool on);
    void showMessage(const QString& title, const QString& message);

    bool isVisible() const { return m_trayIcon->isVisible(); }

signals:
    void showRequested();
    void boostToggled(bool on);
    void overlayToggled(bool on);
    void quitRequested();

private slots:
    void onActivated(QSystemTrayIcon::ActivationReason reason);

private:
    void buildMenu();
    void updateIcon();

    QSystemTrayIcon* m_trayIcon;
    QMenu* m_menu;
    QAction* m_showAction;
    QAction* m_boostAction;
    QAction* m_overlayAction;
    QAction* m_quitAction;

    int m_ping = 0;
    int m_jitter = 0;
    double m_packetLoss = 0.0;
    bool m_degrade_notified_ = false;
    double m_max_ping_alert_ = 150.0;
    double m_max_loss_alert_ = 5.0;
    bool m_connected = false;
    bool m_boostOn = false;
};

} // namespace gno
