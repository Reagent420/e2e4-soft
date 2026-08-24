#pragma once

#include <QWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QButtonGroup>
#include <QLabel>
#include <QIcon>
#include <QPixmap>
#include <QPainter>

namespace gno {

enum class NavPage {
    Dashboard = 0,
    Games,
    Profiles,
    Monitoring,
    Optimizer,
    NetworkTools,
    ProcessMonitor,
    History,
    Diagnostics,
    Remediation,
    GeoMap,
    Settings,
    Count
};

class Sidebar : public QWidget {
    Q_OBJECT

public:
    explicit Sidebar(QWidget* parent = nullptr);
    ~Sidebar() override = default;

    void setNavigationIndex(int index);

signals:
    void navigationChanged(int index);

private:
    QPushButton* createNavButton(NavPage page);
    static QIcon makeIcon(NavPage page, const QColor& color);

    QButtonGroup* m_buttonGroup;
    QLabel* m_logoLabel;
    QLabel* m_versionLabel;
    static constexpr int BUTTON_COUNT = static_cast<int>(NavPage::Count);
};

} // namespace gno
