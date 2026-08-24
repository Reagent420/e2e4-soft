#include "sidebar.h"
#include "theme.h"

#include <QPainterPath>
#include <cmath>

namespace gno {

Sidebar::Sidebar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("sidebar");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 16, 12, 16);
    layout->setSpacing(2);

    m_logoLabel = new QLabel("E2E4", this);
    m_logoLabel->setObjectName("logoLabel");
    m_logoLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_logoLabel);

    m_versionLabel = new QLabel(QStringLiteral("v%1").arg(theme::APP_VERSION), this);
    m_versionLabel->setObjectName("versionLabel");
    m_versionLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_versionLabel);

    layout->addSpacing(8);

    m_buttonGroup = new QButtonGroup(this);
    m_buttonGroup->setExclusive(true);

    m_buttonGroup->addButton(createNavButton(NavPage::Dashboard,       "Р вЂњР В»Р В°Р Р†Р Р…Р В°РЎРЏ"),          0);
    m_buttonGroup->addButton(createNavButton(NavPage::Games,           "Р ВР С–РЎР‚РЎвЂ№"),             1);
    m_buttonGroup->addButton(createNavButton(NavPage::Profiles,        "Р СџРЎР‚Р С•РЎвЂћР С‘Р В»Р С‘ Р С‘Р С–РЎР‚"),      2);
    m_buttonGroup->addButton(createNavButton(NavPage::Monitoring,      "Р СљР С•Р Р…Р С‘РЎвЂљР С•РЎР‚Р С‘Р Р…Р С–"),       3);
    m_buttonGroup->addButton(createNavButton(NavPage::Optimizer,       "Р С›Р С—РЎвЂљР С‘Р СР С‘Р В·Р В°РЎвЂ Р С‘РЎРЏ"),      4);
    m_buttonGroup->addButton(createNavButton(NavPage::NetworkTools,    "Р РЋР ВµРЎвЂљР ВµР Р†РЎвЂ№Р Вµ РЎС“РЎвЂљР С‘Р В»Р С‘РЎвЂљРЎвЂ№"),  5);
    m_buttonGroup->addButton(createNavButton(NavPage::ProcessMonitor,  "Р СџРЎР‚Р С•РЎвЂ Р ВµРЎРѓРЎРѓРЎвЂ№"),         6);
m_buttonGroup->addButton(createNavButton(NavPage::History,         "Р ВРЎРѓРЎвЂљР С•РЎР‚Р С‘РЎРЏ"),         7);
    m_buttonGroup->addButton(createNavButton(NavPage::Diagnostics,     "Р вЂќР С‘Р В°Р С–Р Р…Р С•РЎРѓРЎвЂљР С‘Р С”Р В°"),     8);
    m_buttonGroup->addButton(createNavButton(NavPage::Remediation,   "РћРїС‚РёРјРёР·Р°С†РёСЏ Win"),  9);
    m_buttonGroup->addButton(createNavButton(NavPage::GeoMap,          "Р С™Р В°РЎР‚РЎвЂљР В° РЎРѓР ВµРЎР‚Р Р†Р ВµРЎР‚Р С•Р Р†"), 10);
    m_buttonGroup->addButton(createNavButton(NavPage::Settings,        "Р СњР В°РЎРѓРЎвЂљРЎР‚Р С•Р в„–Р С”Р С‘"), 11);

    for (int i = 0; i < BUTTON_COUNT; ++i) {
        layout->addWidget(m_buttonGroup->button(i));
    }

    layout->addStretch();

    connect(m_buttonGroup, QOverload<int>::of(&QButtonGroup::idClicked),
            this, &Sidebar::navigationChanged);

    m_buttonGroup->button(0)->setChecked(true);
}

QPushButton* Sidebar::createNavButton(NavPage page, const QString& text)
{
    auto* button = new QPushButton(text, this);
    button->setObjectName("sidebarButton");
    button->setCheckable(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setIcon(makeIcon(page, QColor(theme::Colors::TEXT_SECONDARY)));
    button->setIconSize(QSize(20, 20));
    return button;
}

QIcon Sidebar::makeIcon(NavPage page, const QColor& color)
{
    const int sz = 40;
    QPixmap pixmap(sz, sz);
    pixmap.fill(Qt::transparent);

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(color);

    const int m = 8;
    const int s = sz - 2 * m;

    switch (page) {

    case NavPage::Dashboard: {
        int gap = 3;
        int hs = (s - gap) / 2;
        p.drawRoundedRect(QRect(m, m, hs, hs), 3, 3);
        p.drawRoundedRect(QRect(m + hs + gap, m, hs, hs), 3, 3);
        p.drawRoundedRect(QRect(m, m + hs + gap, hs, hs), 3, 3);
        p.drawRoundedRect(QRect(m + hs + gap, m + hs + gap, hs, hs), 3, 3);
        break;
    }

    case NavPage::Games: {
        QRect body(m + 2, m + 4, s - 4, s - 10);
        p.drawRoundedRect(body, 6, 6);
        p.drawRect(m + 6, m + 2, s - 12, 4);
        int cx = sz / 2 - 4;
        int cy = sz / 2 + 2;
        p.drawEllipse(QPoint(cx, cy), 2, 2);
        p.drawEllipse(QPoint(cx + 8, cy), 2, 2);
        break;
    }

    case NavPage::Profiles: {
        p.drawEllipse(QPoint(sz / 2, m + 8), 5, 5);
        QRectF body(m + 8, m + 17, s - 16, m + 10);
        p.drawChord(body, 0 * 16, 180 * 16);
        break;
    }

    case NavPage::Monitoring: {
        QPolygon poly;
        poly << QPoint(m, m + s - 2)
             << QPoint(m + s / 5, m + s / 3)
             << QPoint(m + 2 * s / 5, m + s / 2)
             << QPoint(m + 3 * s / 5, m + 3)
             << QPoint(m + 4 * s / 5, m + s / 4)
             << QPoint(m + s, m + 6);
        p.setPen(QPen(color, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(poly);
        break;
    }

    case NavPage::Optimizer: {
        QPolygon bolt;
        bolt << QPoint(m + s / 3, m)
             << QPoint(m + 2, m + s / 2 + 2)
             << QPoint(m + s / 2, m + s / 2)
             << QPoint(m + s / 3, m + s)
             << QPoint(m + s - 2, m + s / 2 - 2)
             << QPoint(m + s / 2, m + s / 2 + 4);
        p.drawPolygon(bolt);
        break;
    }

    case NavPage::NetworkTools: {
        int cx = sz / 2;
        int cy = sz / 2;
        p.setPen(QPen(color, 2));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPoint(cx, cy), s / 2 - 2, s / 2 - 4);
        p.drawLine(m + 2, cy, m + s - 2, cy);
        p.drawLine(cx, m + 2, cx, m + s - 2);
        p.drawEllipse(QPoint(cx, cy), s / 4, s / 6);
        break;
    }

    case NavPage::ProcessMonitor: {
        int bw = (s - 4) / 3;
        p.drawRoundedRect(QRect(m + 1, m + s / 2, bw, s / 2 - 2), 2, 2);
        p.drawRoundedRect(QRect(m + 2 + bw, m + s / 3, bw, s * 2 / 3 - 2), 2, 2);
        p.drawRoundedRect(QRect(m + 3 + bw * 2, m + 2, bw, s - 4), 2, 2);
        break;
    }

    case NavPage::History: {
        p.drawRect(m + 2, m + 2, s - 4, 4);
        p.drawLine(m + 4, m + 10, m + s - 4, m + 10);
        p.drawLine(m + 4, m + 16, m + s - 4, m + 16);
        p.drawLine(m + 4, m + 22, m + s - 4, m + 22);
        p.drawLine(m + 8, m + 28, m + s - 4, m + 28);
        break;
    }

    case NavPage::Diagnostics: {
        // stethoscope / magnifier
        p.setPen(QPen(color, 2));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPoint(m + 8, m + 8), 6, 6);
        p.drawLine(m + 12, m + 12, m + s - 2, m + s - 2);
        p.drawLine(m + 8, m + 20, m + 8, m + s - 4);
        p.drawEllipse(QPoint(m + 8, m + s - 4), 2, 2);
        break;
    }

    case NavPage::Remediation: {
        // wrench-like: rounded rect + diagonal handle
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawRoundedRect(m + 2, sz / 2 - 3, s - 8, 6, 3, 3);
        p.drawRect(sz / 2 - 3, m + 4, 6, s - 8);
        break;
    }
    case NavPage::GeoMap: {
        p.setPen(QPen(color, 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPoint(sz / 2, sz / 2), s / 2 - 2, s / 2 - 2);
        p.drawLine(m + 4, sz / 2, m + s - 4, sz / 2);
        p.drawEllipse(QPoint(sz / 2 - 4, sz / 2), 4, 4);
        p.drawEllipse(QPoint(sz / 2 + 6, sz / 2 - 4), 3, 3);
        p.drawEllipse(QPoint(sz / 2 - 2, sz / 2 + 6), 2, 2);
        break;
    }

    case NavPage::Settings: {
        int cx = sz / 2;
        int cy = sz / 2;
        int outerR = s / 2 - 1;
        int innerR = outerR - 4;
        int teeth = 6;
        QPainterPath path;
        for (int i = 0; i < teeth * 2; ++i) {
            double angle = M_PI * i / teeth - M_PI / 2;
            double r = (i % 2 == 0) ? outerR : innerR;
            double x = cx + r * cos(angle);
            double y = cy + r * sin(angle);
            if (i == 0) path.moveTo(x, y);
            else path.lineTo(x, y);
        }
        path.closeSubpath();
        p.drawPath(path);
        p.setBrush(QColor(theme::Colors::BG_SURFACE));
        p.drawEllipse(QPoint(cx, cy), 4, 4);
        break;
    }

    case NavPage::Count:
        break;
    }

    p.end();
    return QIcon(pixmap);
}

void Sidebar::setNavigationIndex(int index)
{
    auto* button = m_buttonGroup->button(index);
    if (button) {
        button->setChecked(true);
    }
}

} // namespace gno
