#include "sidebar.h"
#include "theme.h"

namespace gno {

Sidebar::Sidebar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("sidebar");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 16, 12, 16);
    layout->setSpacing(2);

    m_logoLabel = new QLabel("GNO", this);
    m_logoLabel->setObjectName("logoLabel");
    m_logoLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_logoLabel);

    m_versionLabel = new QLabel("v1.0.0", this);
    m_versionLabel->setObjectName("versionLabel");
    m_versionLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_versionLabel);

    layout->addSpacing(8);

    m_buttonGroup = new QButtonGroup(this);
    m_buttonGroup->setExclusive(true);

    m_buttonGroup->addButton(createNavButton("[D]", "Dashboard"), 0);
    m_buttonGroup->addButton(createNavButton("[G]", "Games"), 1);
    m_buttonGroup->addButton(createNavButton("[M]", "Monitoring"), 2);
    m_buttonGroup->addButton(createNavButton("[O]", "Optimizer"), 3);
    m_buttonGroup->addButton(createNavButton("[S]", "Settings"), 4);

    for (int i = 0; i < BUTTON_COUNT; ++i) {
        layout->addWidget(m_buttonGroup->button(i));
    }

    layout->addStretch();

    connect(m_buttonGroup, QOverload<int>::of(&QButtonGroup::idClicked),
            this, &Sidebar::navigationChanged);

    m_buttonGroup->button(0)->setChecked(true);
}

QPushButton* Sidebar::createNavButton(const QString& icon, const QString& text)
{
    auto* button = new QPushButton(QString("%1  %2").arg(icon, text), this);
    button->setObjectName("sidebarButton");
    button->setCheckable(true);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

void Sidebar::setNavigationIndex(int index)
{
    auto* button = m_buttonGroup->button(index);
    if (button) {
        button->setChecked(true);
    }
}

} // namespace gno
