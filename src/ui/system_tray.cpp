#include "system_tray.h"
#include "../core/connection_grader.h"
#include "theme.h"

#include <QPainter>
#include <QFont>

namespace gno {

SystemTray::SystemTray(QObject* parent)
    : QObject(parent)
{
    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setToolTip(QString::fromUtf8("E2E4 Soft — Оптимизатор игровой сети"));

    buildMenu();
    m_trayIcon->setContextMenu(m_menu);

    connect(m_trayIcon, &QSystemTrayIcon::activated,
            this, &SystemTray::onActivated);

    updateIcon();
    m_trayIcon->show();
}

void SystemTray::buildMenu()
{
    m_menu = new QMenu();

    m_showAction = m_menu->addAction(QString::fromUtf8("Показать E2E4 Soft"));
    connect(m_showAction, &QAction::triggered, this, &SystemTray::showRequested);

    m_boostAction = m_menu->addAction(QString::fromUtf8("ОПТИМИЗАЦИЯ: ВЫКЛ"));
    m_boostAction->setCheckable(true);
    connect(m_boostAction, &QAction::triggered, this, [this](bool checked) {
        m_boostOn = checked;
        m_boostAction->setText(checked ? QString::fromUtf8("ОПТИМИЗАЦИЯ: ВКЛ") : QString::fromUtf8("ОПТИМИЗАЦИЯ: ВЫКЛ"));
        emit boostToggled(checked);
    });

    m_overlayAction = m_menu->addAction(QString::fromUtf8("ОВЕРЛЕЙ: ВЫКЛ"));
    m_overlayAction->setCheckable(true);
    connect(m_overlayAction, &QAction::triggered, this, [this](bool checked) {
        m_overlayAction->setText(checked ? QString::fromUtf8("ОВЕРЛЕЙ: ВКЛ") : QString::fromUtf8("ОВЕРЛЕЙ: ВЫКЛ"));
        emit overlayToggled(checked);
    });

    m_menu->addSeparator();

    m_quitAction = m_menu->addAction(QString::fromUtf8("Выход"));
    connect(m_quitAction, &QAction::triggered, this, &SystemTray::quitRequested);

    m_menu->setStyleSheet(
        QString("QMenu { background-color: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 4px; }"
                "QMenu::item { padding: 6px 20px; border-radius: 4px; }"
                "QMenu::item:selected { background-color: %4; }")
            .arg(theme::Colors::BG_SURFACE, theme::Colors::TEXT_PRIMARY,
                 theme::Colors::BORDER, theme::Colors::BG_HOVER));
}

void SystemTray::onActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::DoubleClick || reason == QSystemTrayIcon::Trigger) {
        emit showRequested();
    }
}

void SystemTray::updatePing(int pingMs)
{
    m_ping = pingMs;
    updateIcon();
}

void SystemTray::setAlertThresholds(double maxPing, double maxLoss)
{
    m_max_ping_alert_ = maxPing;
    m_max_loss_alert_ = maxLoss;
}

void SystemTray::updateJitter(int jitterMs)
{
    m_jitter = jitterMs;
}

void SystemTray::updatePacketLoss(double lossPercent)
{
    m_packetLoss = lossPercent;

    // v1.6: live quality score in tooltip + one-shot degradation alert.
    const int score = static_cast<int>(ConnectionGrader::evaluate(
        m_ping, m_jitter, m_packetLoss).score);
    m_trayIcon->setToolTip(QString::fromUtf8("E2E4 Soft - %1/100 | %2 ms | %3%")
        .arg(score).arg(m_ping).arg(m_packetLoss, 0, 'f', 1));

    const bool degraded = m_packetLoss > m_max_loss_alert_ || m_ping > m_max_ping_alert_;
    if (degraded && !m_degrade_notified_) {
        m_degrade_notified_ = true;
        showMessage(QString::fromUtf8("\xD0\x9A\xD0\xB0\xD1\x87\xD0\xB5\xD1\x81\xD1\x82\xD0\xB2\xD0\xBE \xD1\x81\xD0\xB5\xD1\x82\xD0\xB8"),
            QString::fromUtf8("\xD0\x9F\xD0\xBE\xD0\xB2\xD1\x8B\xD1\x88\xD0\xB5\xD0\xBD\xD0\xBD\xD1\x8B\xD0\xB5 \xD0\xBF\xD0\xBE\xD1\x82\xD0\xB5\xD1\x80\xD0\xB8 \xD0\xB8\xD0\xBB\xD0\xB8 \xD0\xB7\xD0\xB0\xD0\xB4\xD0\xB5\xD1\x80\xD0\xB6\xD0\xBA\xD0\xB0 - \xD0\xBF\xD1\x80\xD0\xBE\xD0\xB2\xD0\xB5\xD1\x80\xD1\x8C\xD1\x82\xD0\xB5 \xD0\xB4\xD0\xB8\xD0\xB0\xD0\xB3\xD0\xBD\xD0\xBE\xD1\x81\xD1\x82\xD0\xB8\xD0\xBA\xD1\x83"));
    } else if (!degraded && m_packetLoss < 2.0 && m_ping < 80) {
        m_degrade_notified_ = false;
    }

    updateIcon();
}

void SystemTray::setConnected(bool connected)
{
    m_connected = connected;
    updateIcon();
}

void SystemTray::setBoostOn(bool on)
{
    m_boostOn = on;
    m_boostAction->setChecked(on);
    m_boostAction->setText(on ? QString::fromUtf8("ОПТИМИЗАЦИЯ: ВКЛ") : QString::fromUtf8("ОПТИМИЗАЦИЯ: ВЫКЛ"));
    updateIcon();
}

void SystemTray::setOverlayOn(bool on)
{
    m_overlayAction->setChecked(on);
    m_overlayAction->setText(on ? QString::fromUtf8("ОВЕРЛЕЙ: ВКЛ") : QString::fromUtf8("ОВЕРЛЕЙ: ВЫКЛ"));
}

void SystemTray::showMessage(const QString& title, const QString& message)
{
    if (!m_trayIcon->isVisible())
        return;
    m_trayIcon->showMessage(title, message, QSystemTrayIcon::Information, 4000);
}

void SystemTray::updateIcon()
{
    const int sz = 32;
    QPixmap pixmap(sz, sz);
    pixmap.fill(Qt::transparent);

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);

    QColor bgColor = m_connected ? QColor(theme::Colors::SUCCESS) : QColor(theme::Colors::TEXT_TERTIARY);
    p.setBrush(bgColor);
    p.setPen(Qt::NoPen);
    p.drawEllipse(1, 1, sz - 2, sz - 2);

    p.setPen(QColor(theme::Colors::BG_PRIMARY));
    QFont f("Segoe UI", 9, QFont::Bold);
    p.setFont(f);

    QString text = QString::number(m_ping);
    p.drawText(QRect(0, 0, sz, sz), Qt::AlignCenter, text);

    p.end();

    m_trayIcon->setIcon(QIcon(pixmap));

    QString tooltip = QString::fromUtf8("E2E4 Soft — Оптимизатор игровой сети\n"
                              "Пинг: %1 мс | Джиттер: %2 мс\n"
                              "Потери пакетов: %3%\n"
                              "Оптимизация: %4")
                          .arg(m_ping)
                          .arg(m_jitter)
                          .arg(m_packetLoss, 0, 'f', 1)
                          .arg(m_boostOn ? QString::fromUtf8("ВКЛ") : QString::fromUtf8("ВЫКЛ"));
    m_trayIcon->setToolTip(tooltip);
}

} // namespace gno
