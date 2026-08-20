#include "optimizer.h"
#include "theme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QTimer>
#include <QSettings>

#include "../optimization/fps_optimizer.h"
#include "../optimization/fps_optimizer_windows.h"
#include "../core/network_utils.h"

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
    auto* subtitle = new QLabel("Реальные изменения системы и сети — выберите нужные и нажмите «Применить»");
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
        "Переключает Windows на план «Высокая производительность» (возврат при отключении)", true, fpsGrid, 4);
    fps_checkboxes_[5] = createOptCheckBox("Высокий приоритет процесса",
        "Текущая игра получает больше ресурсов CPU, чем остальные программы", true, fpsGrid, 5);
    fps_checkboxes_[6] = createOptCheckBox("Оптимизация виртуальной памяти",
        "Настраивает системный кэш — меньше подтормаживаний", true, fpsGrid, 6);

    root->addWidget(createSettingsGroup("УСКОРЕНИЕ FPS", fpsGrid));

    // --- Network Optimization section ---
    auto* netGrid = new QGridLayout;
    netGrid->setHorizontalSpacing(12);
    netGrid->setVerticalSpacing(8);

    net_checkboxes_[0] = createOptCheckBox("Мультимаршрутный режим",
        "Данные идут по нескольким путям — ниже пинг и меньше потерь. Требует серверную сеть.", true, netGrid, 0);
    net_checkboxes_[1] = createOptCheckBox("Автовыбор лучшего маршрута",
        "Программа сама переключается на самый быстрый путь. Требует серверную сеть.", true, netGrid, 1);
    net_checkboxes_[2] = createOptCheckBox("Компенсация потерь пакетов",
        "Отправляет дубликаты пакетов при нестабильном соединении. Требует серверную сеть.", true, netGrid, 2);
    net_checkboxes_[3] = createOptCheckBox("Свой DNS-сервер",
        "Быстрый DNS ускоряет подключение к игровым серверам (netsh)", false, netGrid, 3);
    net_checkboxes_[4] = createOptCheckBox("Оптимизация TCP-стека",
        "Адаптивные ACK и параметры TCP снижают задержку в играх (реестр)", true, netGrid, 4);
    net_checkboxes_[5] = createOptCheckBox("Оптимизация MTU",
        "Подбор размера пакета уменьшает фрагментацию и потери (netsh)", true, netGrid, 5);

    // VPN-dependent options: disabled until server infrastructure exists
    for (int i = 0; i < 3; ++i) {
        net_checkboxes_[i]->setEnabled(false);
        net_checkboxes_[i]->setToolTip("Будет доступно после подключения серверной сети (подписка)");
    }

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
    netGrid->addLayout(dnsRow, 6, 0, 1, 2);

    // MTU input row
    auto* mtuRow = new QHBoxLayout;
    mtuRow->setContentsMargins(24, 0, 0, 0);
    auto* mtuLabel = new QLabel("MTU:");
    mtuLabel->setStyleSheet("color:rgba(255,255,255,0.5); font-size:12px; background:transparent;");
    mtu_input_ = new QLineEdit("1400");
    mtu_input_->setObjectName("searchBox");
    mtu_input_->setEnabled(false);
    mtuRow->addWidget(mtuLabel);
    mtuRow->addWidget(mtu_input_);
    netGrid->addLayout(mtuRow, 7, 0, 1, 2);

    connect(net_checkboxes_[3], &QCheckBox::toggled, this, &OptimizerWidget::onDnsToggled);
    connect(net_checkboxes_[5], &QCheckBox::toggled, this, &OptimizerWidget::onMtuToggled);

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
    status_label_->setWordWrap(true);
    status_label_->setAlignment(Qt::AlignCenter);
    root->addWidget(status_label_);

    root->addStretch();

    loadSettings();
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
    s.tcp_optimization = net_checkboxes_[4]->isChecked();
    s.mtu_optimization = net_checkboxes_[5]->isChecked();
    s.dns_server = dns_input_->text();
    bool ok = false;
    int mtu = mtu_input_->text().toInt(&ok);
    s.mtu_value = (ok && mtu >= 576 && mtu <= 1500) ? mtu : 1400;
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
    net_checkboxes_[4]->setChecked(s.tcp_optimization);
    net_checkboxes_[5]->setChecked(s.mtu_optimization);
    dns_input_->setText(s.dns_server);
    dns_input_->setEnabled(s.custom_dns);
    mtu_input_->setText(QString::number(s.mtu_value));
    mtu_input_->setEnabled(s.mtu_optimization);
}

void OptimizerWidget::loadSettings() {
    QSettings settings;
    FPSBoostSettings s;
    s.disable_game_dvr = settings.value("optimizer/gameDvr", true).toBool();
    s.disable_fullscreen_opt = settings.value("optimizer/fullscreenOpt", true).toBool();
    s.disable_mouse_accel = settings.value("optimizer/mouseAccel", true).toBool();
    s.disable_game_mode = settings.value("optimizer/gameMode", false).toBool();
    s.optimize_power_plan = settings.value("optimizer/powerPlan", true).toBool();
    s.set_high_priority = settings.value("optimizer/highPriority", true).toBool();
    s.optimize_virtual_memory = settings.value("optimizer/virtualMemory", true).toBool();
    s.custom_dns = settings.value("optimizer/customDns", false).toBool();
    s.tcp_optimization = settings.value("optimizer/tcpOpt", true).toBool();
    s.mtu_optimization = settings.value("optimizer/mtuOpt", true).toBool();
    s.dns_server = settings.value("optimizer/dnsServer", "1.1.1.1").toString();
    s.mtu_value = settings.value("optimizer/mtuValue", 1400).toInt();
    setSettings(s);
}

void OptimizerWidget::saveSettings() {
    QSettings settings;
    auto s = getSettings();
    settings.setValue("optimizer/gameDvr", s.disable_game_dvr);
    settings.setValue("optimizer/fullscreenOpt", s.disable_fullscreen_opt);
    settings.setValue("optimizer/mouseAccel", s.disable_mouse_accel);
    settings.setValue("optimizer/gameMode", s.disable_game_mode);
    settings.setValue("optimizer/powerPlan", s.optimize_power_plan);
    settings.setValue("optimizer/highPriority", s.set_high_priority);
    settings.setValue("optimizer/virtualMemory", s.optimize_virtual_memory);
    settings.setValue("optimizer/customDns", s.custom_dns);
    settings.setValue("optimizer/tcpOpt", s.tcp_optimization);
    settings.setValue("optimizer/mtuOpt", s.mtu_optimization);
    settings.setValue("optimizer/dnsServer", s.dns_server);
    settings.setValue("optimizer/mtuValue", s.mtu_value);
}

void OptimizerWidget::onApplyClicked() {
    apply_btn_->setEnabled(false);
    apply_btn_->setText("⚡  ПРИМЕНЯЕМ…");

    auto settings = getSettings();
    QStringList applied;
    QStringList warnings;

    // --- real FPS optimizations ---
    gno::FPSBoostConfig cfg;
    cfg.disable_game_dvr = settings.disable_game_dvr;
    cfg.disable_fullscreen_optimizations = settings.disable_fullscreen_opt;
    cfg.disable_mouse_acceleration = settings.disable_mouse_accel;
    cfg.disable_game_mode = settings.disable_game_mode;
    cfg.optimize_power_plan = settings.optimize_power_plan;
    cfg.set_high_priority = settings.set_high_priority;
    cfg.optimize_virtual_memory = settings.optimize_virtual_memory;

    gno::FPSOptimizer optimizer;
    auto result = optimizer.applyConfig(cfg);
    for (const auto& change : result.applied_changes)
        applied << QString::fromStdString(change);
    for (const auto& warn : result.warnings)
        warnings << QString::fromStdString(warn);

    if (settings.optimize_virtual_memory)
        gno::FPSOptimizerPlatform::optimizeVirtualMemory();

    // --- network optimizations ---
    if (settings.custom_dns) {
        QString iface = QString::fromStdString(gno::NetworkUtils::getNetworkInterfaceName());
        if (iface != "default") {
            if (gno::NetworkUtils::setDNS(iface.toStdString(), settings.dns_server.toStdString()))
                applied << QString::fromUtf8("DNS установлен: %1 (%2)").arg(settings.dns_server, iface);
            else
                warnings << QString::fromUtf8("Не удалось установить DNS (нужны права администратора)");
        } else {
            warnings << QString::fromUtf8("Сетевой адаптер не найден — DNS не изменён");
        }
    }

    if (settings.tcp_optimization) {
        if (gno::NetworkUtils::applyTCPOptimizations(true))
            applied << QString::fromUtf8("TCP-стек оптимизирован (адаптивные ACK)");
        else
            warnings << QString::fromUtf8("TCP-оптимизация: часть параметров требует прав администратора");
    }

    if (settings.mtu_optimization) {
        QString iface = QString::fromStdString(gno::NetworkUtils::getNetworkInterfaceName());
        if (iface != "default") {
            if (gno::NetworkUtils::setMTU(iface.toStdString(), settings.mtu_value))
                applied << QString::fromUtf8("MTU установлен: %1").arg(settings.mtu_value);
            else
                warnings << QString::fromUtf8("Не удалось установить MTU (нужны права администратора)");
        } else {
            warnings << QString::fromUtf8("Сетевой адаптер не найден — MTU не изменён");
        }
    }

    saveSettings();
    emit optimizationsApplied();

    // show result
    QString text;
    if (!applied.isEmpty())
        text += QString::fromUtf8("✓ Применено:\n") + applied.join("\n");
    if (!warnings.isEmpty())
        text += QString::fromUtf8("\n\n⚠ ") + warnings.join("\n");
    if (text.isEmpty())
        text = QString::fromUtf8("Ничего не выбрано для применения");

    status_label_->setText(text);
    status_label_->setStyleSheet(
        warnings.isEmpty()
            ? "color:#22c55e; font-size:12px; font-weight:600; background:transparent;"
            : "color:#f59e0b; font-size:12px; font-weight:600; background:transparent;");

    apply_btn_->setEnabled(true);
    apply_btn_->setText("⚡  ПРИМЕНИТЬ ВСЕ НАСТРОЙКИ");
}

void OptimizerWidget::onDnsToggled(bool checked) {
    dns_input_->setEnabled(checked);
}

void OptimizerWidget::onMtuToggled(bool checked) {
    mtu_input_->setEnabled(checked);
}