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
    descLabel->setWordWrap(true);

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
    auto* title = new QLabel("Оптимизация");
    title->setStyleSheet("font-size:20px; font-weight:700; color:white; background:transparent;");
    auto* subtitle = new QLabel("Настройки ускорения FPS и сети — выберите нужные и нажмите «Применить»");
    subtitle->setStyleSheet("color:rgba(255,255,255,0.4); font-size:12px; background:transparent;");
    headerRow->addWidget(title);
    headerRow->addStretch();
    headerRow->addWidget(subtitle);
    root->addLayout(headerRow);

    // --- FPS Boost section ---
    auto* fpsGrid = new QGridLayout;
    fpsGrid->setHorizontalSpacing(12);
    fpsGrid->setVerticalSpacing(8);

    fps_checkboxes_[0] = createOptCheckBox("Отключить запись игр (Game DVR)",
        "Останавливает фоновую запись Windows, которая отнимает FPS", true, fpsGrid, 0);
    fps_checkboxes_[1] = createOptCheckBox("Отключить оптимизации полноэкранного режима",
        "Убирает задержки ввода в играх (работает не со всеми играми)", true, fpsGrid, 1);
    fps_checkboxes_[2] = createOptCheckBox("Отключить ускорение мыши",
        "Прямой ввод с мыши — точнее прицеливание в шутерах", true, fpsGrid, 2);
    fps_checkboxes_[3] = createOptCheckBox("Отключить игровой режим Windows",
        "Иногда вызывает микролаги — отключаем для стабильности", false, fpsGrid, 3);
    fps_checkboxes_[4] = createOptCheckBox("Максимальная производительность питания",
        "Переключает Windows на план «Высокая производительность»", true, fpsGrid, 4);
    fps_checkboxes_[5] = createOptCheckBox("Высокий приоритет процесса",
        "Игра получает больше ресурсов CPU, чем остальные программы", true, fpsGrid, 5);
    fps_checkboxes_[6] = createOptCheckBox("Оптимизация виртуальной памяти",
        "Настраивает файл подкачки — меньше подтормаживаний", true, fpsGrid, 6);

    root->addWidget(createSettingsGroup("УСКОРЕНИЕ FPS", fpsGrid));

    // --- Network Optimization section ---
    auto* netGrid = new QGridLayout;
    netGrid->setHorizontalSpacing(12);
    netGrid->setVerticalSpacing(8);

    net_checkboxes_[0] = createOptCheckBox("Мультимаршрутный режим",
        "Данные идут по нескольким путям — ниже пинг и меньше потерь", true, netGrid, 0);
    net_checkboxes_[1] = createOptCheckBox("Автовыбор лучшего маршрута",
        "Программа сама переключается на самый быстрый путь", true, netGrid, 1);
    net_checkboxes_[2] = createOptCheckBox("Компенсация потерь пакетов",
        "Отправляет дубликаты пакетов при нестабильном соединении", true, netGrid, 2);
    net_checkboxes_[3] = createOptCheckBox("Свой DNS-сервер",
        "Быстрый DNS ускоряет подключение к игровым серверам", false, netGrid, 3);

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

    root->addWidget(createSettingsGroup("ОПТИМИЗАЦИЯ СЕТИ", netGrid));

    // --- Apply button ---
    apply_btn_ = new QPushButton("⚡  ПРИМЕНИТЬ ВСЕ НАСТРОЙКИ");
    apply_btn_->setObjectName("boostButton");
    apply_btn_->setFixedHeight(52);
    apply_btn_->setCursor(Qt::PointingHandCursor);
    connect(apply_btn_, &QPushButton::clicked, this, &OptimizerWidget::onApplyClicked);
    root->addWidget(apply_btn_);

    // --- Status label ---
    status_label_ = new QLabel("Готово к применению");
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
    apply_btn_->setText("⚡  ПРИМЕНЯЕМ…");

    QTimer::singleShot(1200, this, [this]() {
        apply_btn_->setEnabled(true);
        apply_btn_->setText("⚡  ПРИМЕНИТЬ ВСЕ НАСТРОЙКИ");
        status_label_->setText("✓ Все настройки применены");
        status_label_->setStyleSheet("color:#22c55e; font-size:12px; font-weight:600; background:transparent;");
        emit optimizationsApplied();
    });
}

void OptimizerWidget::onDnsToggled(bool checked) {
    dns_input_->setEnabled(checked);
}