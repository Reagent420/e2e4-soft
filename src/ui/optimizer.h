#pragma once

#include <QWidget>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>

struct FPSBoostSettings {
    bool disable_game_dvr = true;
    bool disable_fullscreen_opt = true;
    bool disable_mouse_accel = true;
    bool disable_game_mode = false;
    bool optimize_power_plan = true;
    bool set_high_priority = true;
    bool optimize_virtual_memory = true;
    bool multipath_routing = true;
    bool real_time_route = true;
    bool packet_loss_compensation = true;
    bool custom_dns = false;
    QString dns_server = "1.1.1.1";
};

class OptimizerWidget : public QWidget {
    Q_OBJECT
public:
    explicit OptimizerWidget(QWidget* parent = nullptr);
    FPSBoostSettings getSettings() const;
    void setSettings(const FPSBoostSettings& settings);

signals:
    void optimizationsApplied();

private slots:
    void onApplyClicked();
    void onDnsToggled(bool checked);

private:
    QCheckBox* fps_checkboxes_[7];
    QCheckBox* net_checkboxes_[4];
    QLineEdit* dns_input_;
    QPushButton* apply_btn_;
    QLabel* status_label_;
};
