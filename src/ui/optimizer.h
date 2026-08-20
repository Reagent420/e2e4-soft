#pragma once

#include <QWidget>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QVector>

#include "../core/system_audit.h"

namespace gno {

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
    bool tcp_optimization = true;
    bool mtu_optimization = true;
    QString dns_server = "1.1.1.1";
    int mtu_value = 1400;
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
    void onMtuToggled(bool checked);

private:
    void loadSettings();
    void saveSettings();
    void renderChanges(const QVector<SettingChange>& changes);
    void renderCapabilities();
    QWidget* makeChangeCard(const SettingChange& c);
    QWidget* makeCapabilityCard(const Capability& c);

    QCheckBox* fps_checkboxes_[7];
    QCheckBox* net_checkboxes_[6];
    QLineEdit* dns_input_;
    QLineEdit* mtu_input_;
    QPushButton* apply_btn_;
    QLabel* status_label_;
    QWidget* changes_list_;
    QWidget* caps_list_;
};

} // namespace gno