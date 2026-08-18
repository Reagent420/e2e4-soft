#include "settings_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QDialogButtonBox>

namespace gno {
namespace ui {

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("Settings");
    setMinimumSize(500, 600);
    setupUI();
}

SettingsDialog::~SettingsDialog() = default;

void SettingsDialog::setupUI() {
    QVBoxLayout* main_layout = new QVBoxLayout(this);
    
    tab_widget_ = new QTabWidget();
    setupGeneralTab();
    setupFPSBoostTab();
    setupMultipathTab();
    setupAdvancedTab();
    
    main_layout->addWidget(tab_widget_);
    
    QHBoxLayout* button_layout = new QHBoxLayout();
    
    defaults_button_ = new QPushButton("Restore Defaults");
    connect(defaults_button_, &QPushButton::clicked, this, &SettingsDialog::onRestoreDefaults);
    button_layout->addWidget(defaults_button_);
    
    button_layout->addStretch();
    
    reset_button_ = new QPushButton("Reset");
    connect(reset_button_, &QPushButton::clicked, this, &SettingsDialog::onResetClicked);
    button_layout->addWidget(reset_button_);
    
    apply_button_ = new QPushButton("Apply");
    connect(apply_button_, &QPushButton::clicked, this, &SettingsDialog::onApplyClicked);
    button_layout->addWidget(apply_button_);
    
    main_layout->addLayout(button_layout);
}

void SettingsDialog::setupGeneralTab() {
    QWidget* tab = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(tab);
    
    auto_start_checkbox_ = new QCheckBox("Start with Windows");
    layout->addWidget(auto_start_checkbox_);
    
    minimize_to_tray_checkbox_ = new QCheckBox("Minimize to system tray");
    minimize_to_tray_checkbox_->setChecked(true);
    layout->addWidget(minimize_to_tray_checkbox_);
    
    notifications_checkbox_ = new QCheckBox("Show notifications");
    notifications_checkbox_->setChecked(true);
    layout->addWidget(notifications_checkbox_);
    
    launch_with_windows_checkbox_ = new QCheckBox("Launch minimized");
    layout->addWidget(launch_with_windows_checkbox_);
    
    layout->addStretch();
    tab_widget_->addTab(tab, "General");
}

void SettingsDialog::setupFPSBoostTab() {
    QWidget* tab = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(tab);
    
    QGroupBox* game_dvr_group = new QGroupBox("Game DVR");
    QVBoxLayout* game_dvr_layout = new QVBoxLayout(game_dvr_group);
    game_dvr_checkbox_ = new QCheckBox("Disable Game DVR");
    game_dvr_checkbox_->setChecked(true);
    game_dvr_layout->addWidget(game_dvr_checkbox_);
    layout->addWidget(game_dvr_group);
    
    QGroupBox* display_group = new QGroupBox("Display");
    QVBoxLayout* display_layout = new QVBoxLayout(display_group);
    fullscreen_opt_checkbox_ = new QCheckBox("Disable fullscreen optimizations");
    fullscreen_opt_checkbox_->setChecked(true);
    display_layout->addWidget(fullscreen_opt_checkbox_);
    layout->addWidget(display_group);
    
    QGroupBox* input_group = new QGroupBox("Input");
    QVBoxLayout* input_layout = new QVBoxLayout(input_group);
    mouse_accel_checkbox_ = new QCheckBox("Disable mouse acceleration");
    mouse_accel_checkbox_->setChecked(true);
    input_layout->addWidget(mouse_accel_checkbox_);
    layout->addWidget(input_group);
    
    QGroupBox* power_group = new QGroupBox("Power");
    QVBoxLayout* power_layout = new QVBoxLayout(power_group);
    power_plan_checkbox_ = new QCheckBox("Optimize power plan");
    power_plan_checkbox_->setChecked(true);
    power_layout->addWidget(power_plan_checkbox_);
    
    QHBoxLayout* plan_layout = new QHBoxLayout();
    plan_layout->addWidget(new QLabel("Power plan:"));
    power_plan_combo_ = new QComboBox();
    power_plan_combo_->addItem("High Performance");
    power_plan_combo_->addItem("Balanced");
    power_plan_combo_->addItem("Power Saver");
    plan_layout->addWidget(power_plan_combo_);
    power_layout->addLayout(plan_layout);
    layout->addWidget(power_group);
    
    QGroupBox* process_group = new QGroupBox("Process");
    QVBoxLayout* process_layout = new QVBoxLayout(process_group);
    high_priority_checkbox_ = new QCheckBox("Set game to high priority");
    high_priority_checkbox_->setChecked(true);
    process_layout->addWidget(high_priority_checkbox_);
    layout->addWidget(process_group);
    
    QGroupBox* memory_group = new QGroupBox("Memory");
    QVBoxLayout* memory_layout = new QVBoxLayout(memory_group);
    virtual_memory_checkbox_ = new QCheckBox("Optimize virtual memory");
    virtual_memory_checkbox_->setChecked(true);
    memory_layout->addWidget(virtual_memory_checkbox_);
    layout->addWidget(memory_group);
    
    layout->addStretch();
    tab_widget_->addTab(tab, "FPS Boost");
}

void SettingsDialog::setupMultipathTab() {
    QWidget* tab = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(tab);
    
    QGroupBox* routing_group = new QGroupBox("Routing");
    QFormLayout* routing_layout = new QFormLayout(routing_group);
    
    max_paths_spin_ = new QSpinBox();
    max_paths_spin_->setRange(1, 10);
    max_paths_spin_->setValue(5);
    routing_layout->addRow("Max paths:", max_paths_spin_);
    
    probe_interval_spin_ = new QSpinBox();
    probe_interval_spin_->setRange(100, 10000);
    probe_interval_spin_->setValue(1000);
    probe_interval_spin_->setSuffix(" ms");
    routing_layout->addRow("Probe interval:", probe_interval_spin_);
    
    switch_threshold_spin_ = new QSpinBox();
    switch_threshold_spin_->setRange(10, 200);
    switch_threshold_spin_->setValue(50);
    switch_threshold_spin_->setSuffix(" ms");
    routing_layout->addRow("Switch threshold:", switch_threshold_spin_);
    
    loss_threshold_spin_ = new QSpinBox();
    loss_threshold_spin_->setRange(1, 20);
    loss_threshold_spin_->setValue(5);
    loss_threshold_spin_->setSuffix("%");
    routing_layout->addRow("Loss threshold:", loss_threshold_spin_);
    
    layout->addWidget(routing_group);
    
    QGroupBox* auto_group = new QGroupBox("Automatic");
    QVBoxLayout* auto_layout = new QVBoxLayout(auto_group);
    auto_switch_checkbox_ = new QCheckBox("Auto-switch routes");
    auto_switch_checkbox_->setChecked(true);
    auto_layout->addWidget(auto_switch_checkbox_);
    
    load_balance_checkbox_ = new QCheckBox("Load balance between paths");
    auto_layout->addWidget(load_balance_checkbox_);
    layout->addWidget(auto_group);
    
    layout->addStretch();
    tab_widget_->addTab(tab, "Multipath");
}

void SettingsDialog::setupAdvancedTab() {
    QWidget* tab = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(tab);
    
    QGroupBox* debug_group = new QGroupBox("Debug");
    QVBoxLayout* debug_layout = new QVBoxLayout(debug_group);
    debug_mode_checkbox_ = new QCheckBox("Enable debug mode");
    debug_layout->addWidget(debug_mode_checkbox_);
    
    QHBoxLayout* log_layout = new QHBoxLayout();
    log_layout->addWidget(new QLabel("Log level:"));
    log_level_combo_ = new QComboBox();
    log_level_combo_->addItems({"Error", "Warning", "Info", "Debug", "Trace"});
    log_level_combo_->setCurrentIndex(2);
    log_layout->addWidget(log_level_combo_);
    debug_layout->addLayout(log_layout);
    
    QHBoxLayout* log_path_layout = new QHBoxLayout();
    log_path_layout->addWidget(new QLabel("Log path:"));
    log_path_edit_ = new QLineEdit("logs/");
    log_path_layout->addWidget(log_path_edit_);
    debug_layout->addLayout(log_path_layout);
    layout->addWidget(debug_group);
    
    layout->addStretch();
    tab_widget_->addTab(tab, "Advanced");
}

FPSBoostConfig SettingsDialog::getFPSBoostConfig() const {
    FPSBoostConfig config;
    config.disable_game_dvr = game_dvr_checkbox_->isChecked();
    config.disable_fullscreen_optimizations = fullscreen_opt_checkbox_->isChecked();
    config.disable_mouse_acceleration = mouse_accel_checkbox_->isChecked();
    config.optimize_power_plan = power_plan_checkbox_->isChecked();
    config.set_high_priority = high_priority_checkbox_->isChecked();
    config.optimize_virtual_memory = virtual_memory_checkbox_->isChecked();
    config.power_plan_mode = power_plan_combo_->currentIndex();
    return config;
}

MultipathConfig SettingsDialog::getMultipathConfig() const {
    MultipathConfig config;
    config.max_paths = max_paths_spin_->value();
    config.probe_interval_ms = probe_interval_spin_->value();
    config.switch_threshold_ms = switch_threshold_spin_->value();
    config.loss_threshold_percent = loss_threshold_spin_->value();
    config.auto_switch = auto_switch_checkbox_->isChecked();
    config.load_balance = load_balance_checkbox_->isChecked();
    return config;
}

void SettingsDialog::onApplyClicked() {
    emit settingsChanged();
    accept();
}

void SettingsDialog::onResetClicked() {
    game_dvr_checkbox_->setChecked(true);
    fullscreen_opt_checkbox_->setChecked(true);
    mouse_accel_checkbox_->setChecked(true);
    power_plan_checkbox_->setChecked(true);
    high_priority_checkbox_->setChecked(true);
    virtual_memory_checkbox_->setChecked(true);
    
    max_paths_spin_->setValue(5);
    probe_interval_spin_->setValue(1000);
    switch_threshold_spin_->setValue(50);
    loss_threshold_spin_->setValue(5);
    auto_switch_checkbox_->setChecked(true);
    load_balance_checkbox_->setChecked(false);
}

void SettingsDialog::onRestoreDefaults() {
    onResetClicked();
}

} // namespace ui
} // namespace gno
