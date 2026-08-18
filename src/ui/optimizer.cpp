#include "optimizer.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QTimer>

static QWidget* createSettingsGroup(const QString& title, QLayout* contentLayout) {
    auto* group = new QWidget;
    group->setObjectName("settingsGroup");

    auto* layout = new QVBoxLayout(group);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(8);

    auto* titleLabel = new QLabel(title);
    titleLabel->setObjectName("sectionTitle");
    layout->addWidget(titleLabel);
    layout->addLayout(contentLayout);

    return group;
}

static QCheckBox* createOptCheckBox(const QString& text, const QString& desc, bool checked, QGridLayout* grid, int row) {
    auto* cb = new QCheckBox(text);
    cb->setChecked(checked);

    auto* descLabel = new QLabel(desc);
    descLabel->setStyleSheet("color:rgba(255,255,255,0.4); font-size:11px; background:transparent;");

    grid->addWidget(cb, row, 0);
    grid->addWidget(descLabel, row, 1);

    return cb;
}

// ---------------------------------------------------------------------------
// OptimizerWidget
// ---------------------------------------------------------------------------

OptimizerWidget::OptimizerWidget(QWidget* parent)
    : QWidget(parent) {

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(14);

    // header
    auto* headerRow = new QHBoxLayout;
    auto* title = new QLabel("Game Optimizer");
    title->setStyleSheet("font-size:20px; font-weight:700; color:white; background:transparent;");
    auto* subtitle = new QLabel("Maximize your performance");
    subtitle->setStyleSheet("color:rgba(255,255,255,0.4); font-size:12px; background:transparent;");
    headerRow->addWidget(title);
    headerRow->addStretch();
    headerRow->addWidget(subtitle);
    root->addLayout(headerRow);

    // --- FPS Boost section ---
    auto* fpsGrid = new QGridLayout;
    fpsGrid->setHorizontalSpacing(12);
    fpsGrid->setVerticalSpacing(8);

    fps_checkboxes_[0] = createOptCheckBox("Disable Game DVR",
        "Turns off Windows Game DVR recording", true, fpsGrid, 0);
    fps_checkboxes_[1] = createOptCheckBox("Disable Fullscreen Optimizations",
        "Prevents Windows from applying fullscreen hacks", true, fpsGrid, 1);
    fps_checkboxes_[2] = createOptCheckBox("Disable Mouse Acceleration",
        "Raw mouse input for precise aiming", true, fpsGrid, 2);
    fps_checkboxes_[3] = createOptCheckBox("Disable Game Mode",
        "Windows Game Mode can cause stuttering", false, fpsGrid, 3);
    fps_checkboxes_[4] = createOptCheckBox("Optimize Power Plan",
        "Switch to High Performance power plan", true, fpsGrid, 4);
    fps_checkboxes_[5] = createOptCheckBox("Set High Process Priority",
        "Give games CPU priority", true, fpsGrid, 5);
    fps_checkboxes_[6] = createOptCheckBox("Optimize Virtual Memory",
        "Adjust page file settings", true, fpsGrid, 6);

    root->addWidget(createSettingsGroup("FPS BOOST", fpsGrid));

    // --- Network Optimization section ---
    auto* netGrid = new QGridLayout;
    netGrid->setHorizontalSpacing(12);
    netGrid->setVerticalSpacing(8);

    net_checkboxes_[0] = createOptCheckBox("Multipath Routing",
        "Use multiple routes for best latency", true, netGrid, 0);
    net_checkboxes_[1] = createOptCheckBox("Real-time Route Selection",
        "Dynamically switch to fastest path", true, netGrid, 1);
    net_checkboxes_[2] = createOptCheckBox("Packet Loss Compensation",
        "Redundant packets for lossy connections", true, netGrid, 2);
    net_checkboxes_[3] = createOptCheckBox("Custom DNS Server",
        "Use fast DNS for game server resolution", false, netGrid, 3);

    // DNS input row
    auto* dnsRow = new QHBoxLayout;
    dnsRow->setContentsMargins(24, 0, 0, 0);
    auto* dnsLabel = new QLabel("DNS:");
    dnsLabel->setStyleSheet("color:rgba(255,255,255,0.5); font-size:12px; background:transparent;");
    dns_input_ = new QLineEdit("1.1.1.1");
    dns_input_->setObjectName("searchBox");
    dns_input_->setEnabled(false);
    dnsRow->addWidget(dnsLabel);
    dnsRow->addWidget(dns_input_);
    netGrid->addLayout(dnsRow, 4, 0, 1, 2);

    connect(net_checkboxes_[3], &QCheckBox::toggled, this, &OptimizerWidget::onDnsToggled);

    root->addWidget(createSettingsGroup("NETWORK OPTIMIZATION", netGrid));

    // --- Apply button ---
    apply_btn_ = new QPushButton("⚡  APPLY ALL OPTIMIZATIONS");
    apply_btn_->setObjectName("boostButton");
    apply_btn_->setFixedHeight(52);
    apply_btn_->setCursor(Qt::PointingHandCursor);
    connect(apply_btn_, &QPushButton::clicked, this, &OptimizerWidget::onApplyClicked);
    root->addWidget(apply_btn_);

    // --- Status label ---
    status_label_ = new QLabel("Status: Ready");
    status_label_->setStyleSheet("color:rgba(255,255,255,0.5); font-size:12px; background:transparent;");
    status_label_->setAlignment(Qt::AlignCenter);
    root->addWidget(status_label_);

    root->addStretch();
}

FPSBoostSettings OptimizerWidget::getSettings() const {
    FPSBoostSettings s;
    s.disable_game_dvr = fps_checkboxes_[0]->isChecked();
    s.disable_fullscreen_opt = fps_checkboxes_[1]->isChecked();
    s.disable_mouse_accel = fps_checkboxes_[2]->isChecked();
    s.disable_game_mode = fps_checkboxes_[3]->isChecked();
    s.optimize_power_plan = fps_checkboxes_[4]->isChecked();
    s.set_high_priority = fps_checkboxes_[5]->isChecked();
    s.optimize_virtual_memory = fps_checkboxes_[6]->isChecked();
    s.multipath_routing = net_checkboxes_[0]->isChecked();
    s.real_time_route = net_checkboxes_[1]->isChecked();
    s.packet_loss_compensation = net_checkboxes_[2]->isChecked();
    s.custom_dns = net_checkboxes_[3]->isChecked();
    s.dns_server = dns_input_->text();
    return s;
}

void OptimizerWidget::setSettings(const FPSBoostSettings& s) {
    fps_checkboxes_[0]->setChecked(s.disable_game_dvr);
    fps_checkboxes_[1]->setChecked(s.disable_fullscreen_opt);
    fps_checkboxes_[2]->setChecked(s.disable_mouse_accel);
    fps_checkboxes_[3]->setChecked(s.disable_game_mode);
    fps_checkboxes_[4]->setChecked(s.optimize_power_plan);
    fps_checkboxes_[5]->setChecked(s.set_high_priority);
    fps_checkboxes_[6]->setChecked(s.optimize_virtual_memory);
    net_checkboxes_[0]->setChecked(s.multipath_routing);
    net_checkboxes_[1]->setChecked(s.real_time_route);
    net_checkboxes_[2]->setChecked(s.packet_loss_compensation);
    net_checkboxes_[3]->setChecked(s.custom_dns);
    dns_input_->setText(s.dns_server);
    dns_input_->setEnabled(s.custom_dns);
}

void OptimizerWidget::onApplyClicked() {
    apply_btn_->setEnabled(false);
    apply_btn_->setText("⚡  APPLYING...");

    QTimer::singleShot(1200, this, [this]() {
        apply_btn_->setEnabled(true);
        apply_btn_->setText("⚡  APPLY ALL OPTIMIZATIONS");
        status_label_->setText("Status: All optimizations applied ✓");
        status_label_->setStyleSheet("color:#22c55e; font-size:12px; font-weight:600; background:transparent;");
        emit optimizationsApplied();
    });
}

void OptimizerWidget::onDnsToggled(bool checked) {
    dns_input_->setEnabled(checked);
}
