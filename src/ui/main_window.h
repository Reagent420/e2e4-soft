#pragma once

#include <QMainWindow>
#include <QStackedWidget>
#include <QStatusBar>
#include <QLabel>

namespace gno {

class Sidebar;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override = default;

private slots:
    void onNavigationChanged(int index);
    void updateConnectionStatus(const QString& status);

private:
    void setupUi();
    void setupPages();

    Sidebar* m_sidebar;
    QStackedWidget* m_stackedWidget;
    QLabel* m_statusLabel;
};

} // namespace gno
