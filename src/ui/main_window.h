#pragma once

#include <QMainWindow>
#include <QStackedWidget>
#include <QStatusBar>
#include <QLabel>
#include <QCloseEvent>

namespace gno {

class Sidebar;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override = default;

    void forceShow();

signals:
    void themeChanged(bool dark);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onNavigationChanged(int index);

private:
    void setupUi();
    void setupPages();

    Sidebar* m_sidebar;
    QStackedWidget* m_stackedWidget;
    QLabel* m_statusLabel;
};

} // namespace gno
