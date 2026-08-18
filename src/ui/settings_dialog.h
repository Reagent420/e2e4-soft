#pragma once

#include <QDialog>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include "../optimization/fps_optimizer.h"
#include "../optimization/system_tweaks.h"
#include "../core/multipath_engine.h"

namespace gno {
namespace ui {

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);
    ~SettingsDialog();

    FPSBoostConfig getFPSBoostConfig() const;
    MultipathConfig getMultipathConfig() const;

signals:
    void settingsChanged();

private slots:
    void onApplyClicked();
    void onResetClicked();
    void onRestoreDefaults();

private:
    void setupUI();
    void setupGeneralTab();
    void setupFPSBoostTab();
    void setupMultipathTab();
    void setupAdvancedTab();

    QTabWidget* tab_widget_;
    
    QCheckBox* auto_start_checkbox_;
    QCheckBox* minimize_to_tray_checkbox_;
    QCheckBox* notifications_checkbox_;
    QCheckBox* launch_with_windows_checkbox_;
    
    QCheckBox* game_dvr_checkbox_;
    QCheckBox* fullscreen_opt_checkbox_;
    QCheckBox* mouse_accel_checkbox_;
    QCheckBox* power_plan_checkbox_;
    QCheckBox* high_priority_checkbox_;
    QCheckBox* virtual_memory_checkbox_;
    QComboBox* power_plan_combo_;
    
    QSpinBox* max_paths_spin_;
    QSpinBox* probe_interval_spin_;
    QSpinBox* switch_threshold_spin_;
    QSpinBox* loss_threshold_spin_;
    QCheckBox* auto_switch_checkbox_;
    QCheckBox* load_balance_checkbox_;
    
    QCheckBox* debug_mode_checkbox_;
    QComboBox* log_level_combo_;
    QLineEdit* log_path_edit_;
    
    QPushButton* apply_button_;
    QPushButton* reset_button_;
    QPushButton* defaults_button_;
    
    FPSBoostConfig default_fps_config_;
    MultipathConfig default_multipath_config_;
};

} // namespace ui
} // namespace gno
