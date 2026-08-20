#include "system_tray.h"
#include "theme.h"

#include <QPainter>
#include <QFont>

namespace gno {

SystemTray::SystemTray(QObject* parent)
    : QObject(parent)
{
    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setToolTip(QString::fromUtf8("GNO — Оптимизатор игровой сети"));

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

    m_showAction = m_menu->addAction(QString::fromUtf8("Показать GNO"));
    connect(m_showAction, &QAction::triggered, this, &SystemTray::showRequested);

    m_boostAction = m_menu->addAction(QString::fromUtf8("ОПТИМИЗАЦИЯ: ВЫКЛ"));
    m_boostAction->setCheckable(true);
    connect(m_boostAction, &QAction::triggered, this, [this](bool checked) {
        m_boostOn = checked;
        m_boostAction->setText(checked ? QString::fromUtf8("ОПТИМИЗАЦИЯ: ВКЛ") : QString::fromUtf8("ОПТИМИЗАЦИЯ: ВЫКЛ"));
        emit boostToggled(checked);
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

void SystemTray::updateJitter(int jitterMs)
{
    m_jitter = jitterMs;
}

void SystemTray::updatePacketLoss(double lossPercent)
{
    m_packetLoss = lossPercent;
}

void SystemTray::setConnected(bool connected)
{
    m_connected = connected;
    updateIcon();
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

    QString tooltip = QString::fromUtf8("GNO — Оптимизатор игровой сети\n"
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
