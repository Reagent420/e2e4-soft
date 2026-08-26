#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>

namespace gno {

class FpsBoostWidget : public QWidget {
    Q_OBJECT
public:
    explicit FpsBoostWidget(QWidget* parent = nullptr);

private slots:
    void onTimerToggle();
    void onRamClean();
    void onStartupToggle(int row, int col);
    void onRefreshServices();

private:
    void setupUI();

    QPushButton* m_timer_btn_ = nullptr;
    QLabel* m_timer_label_ = nullptr;
    QPushButton* m_ram_btn_ = nullptr;
    QLabel* m_ram_label_ = nullptr;
    QTableWidget* m_services_table_ = nullptr;
    QTableWidget* m_startup_table_ = nullptr;
};

} // namespace gno
