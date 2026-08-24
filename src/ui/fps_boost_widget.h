#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>

namespace gno {

// FPS Boost page: timer resolution toggle, RAM cleaner,
// service management (SysMain/Search/DoSvc), startup programs.
class FpsBoostWidget : public QWidget {
    Q_OBJECT
public:
    explicit FpsBoostWidget(QWidget* parent = nullptr);

private slots:
    void onTimerToggle();
    void onRamClean();
    void onServiceToggle();
    void onStartupToggle();
    void onRefreshServices();
    void onRefreshStartup();

private:
    void setupUI();
    void m_table_style_helper(class QTableWidget*);
private slots:
    void onStartupToggle(int row, int col);

    QPushButton* m_timer_btn_;
    QLabel* m_timer_label_;
    QPushButton* m_ram_btn_;
    QLabel* m_ram_label_;
    QTableWidget* m_services_table_;
    QTableWidget* m_startup_table_;
};

} // namespace gno
