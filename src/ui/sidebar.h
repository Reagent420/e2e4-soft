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

// Новая IA (GNO_CONCEPT 8+1): 8 секций + модальные настройки.
// Бывшие 14 пунктов стали представлениями (вкладками) внутри секций:
//   Games        ← Games + Profiles
//   Network      ← Monitoring + NetworkTools + Diagnostics
//   System       ← ProcessMonitor + FineTune
//   Optimizations← Remediation + Optimizer
//   Servers      ← GeoMap
//   History      ← History
//   Knowledge    ← новая секция (problem_db)
//   Settings     ← модалка (не секция)
// FPS Boost выведен из навигации (плацебо-риск; действия переедут в Optimizations, P10).
enum class NavPage {
    Dashboard = 0,
    Games,
    Network,
    System,
    Optimizations,
    Servers,
    History,
    Knowledge,
    Count,
    Settings = 100  // виртуальный id кнопки настроек (вне стека страниц)
};

class Sidebar : public QWidget {
    Q_OBJECT

public:
    explicit Sidebar(QWidget* parent = nullptr);
    ~Sidebar() override = default;

    void setNavigationIndex(int index);

signals:
    void navigationChanged(int index);
    void settingsRequested();

private:
    QPushButton* createNavButton(NavPage page);
    static QIcon makeIcon(NavPage page, const QColor& color);

    QButtonGroup* m_buttonGroup;
    QPushButton* m_settingsButton = nullptr;
    QLabel* m_logoLabel;
    QLabel* m_versionLabel;
    static constexpr int BUTTON_COUNT = static_cast<int>(NavPage::Count);
};

} // namespace gno
