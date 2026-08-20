#include "optimizer.h"
#include "theme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QTimer>
#include <QSettings>
#include <QScrollArea>
#include <QFrame>
#include <QHash>

#include "../optimization/fps_optimizer.h"
#include "../optimization/fps_optimizer_windows.h"
#include "../core/network_utils.h"
#include "../core/system_audit.h"

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

namespace gno {

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

    // --- What was applied (old -> new) ---
    auto* changesTitle = new QLabel("Что применилось (до → после)");
    changesTitle->setStyleSheet("font-size:14px; font-weight:700; color:white; background:transparent;");
    root->addWidget(changesTitle);

    auto* changesScroll = new QScrollArea;
    changesScroll->setWidgetResizable(true);
    changesScroll->setFrameShape(QFrame::NoFrame);
    changesScroll->setMaximumHeight(260);
    changes_list_ = new QWidget;
    changes_list_->setLayout(new QVBoxLayout(changes_list_));
    changes_list_->layout()->setContentsMargins(0, 0, 0, 0);
    changes_list_->layout()->setSpacing(6);
    changesScroll->setWidget(changes_list_);
    root->addWidget(changesScroll);

    // --- Capability matrix ---
    auto* capsTitle = new QLabel("Что программа может и не может сделать");
    capsTitle->setStyleSheet("font-size:14px; font-weight:700; color:white; background:transparent;");
    root->addWidget(capsTitle);

    auto* capsScroll = new QScrollArea;
    capsScroll->setWidgetResizable(true);
    capsScroll->setFrameShape(QFrame::NoFrame);
    capsScroll->setMaximumHeight(300);
    caps_list_ = new QWidget;
    caps_list_->setLayout(new QVBoxLayout(caps_list_));
    caps_list_->layout()->setContentsMargins(0, 0, 0, 0);
    caps_list_->layout()->setSpacing(6);
    capsScroll->setWidget(caps_list_);
    root->addWidget(capsScroll);

    root->addStretch();

    renderCapabilities();
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

    // capture BEFORE state so we can show a real diff
    QStringList beforeKeys = {
        "GameDVR", "FullscreenOpt", "GameMode", "PowerPlan", "TcpAck", "TcpNoDelay"
    };
    QHash<QString, QString> before;
    before["GameDVR"] = QString::fromStdString(SystemAudit::readGameDvrValue());
    before["FullscreenOpt"] = QString::fromStdString(SystemAudit::readFullscreenOptValue());
    before["GameMode"] = QString::fromStdString(SystemAudit::readGameModeValue());
    before["PowerPlan"] = QString::fromStdString(SystemAudit::readActivePowerPlan());
    before["TcpAck"] = QString::fromStdString(SystemAudit::readTcpValue("TcpAckFrequency"));
    before["TcpNoDelay"] = QString::fromStdString(SystemAudit::readTcpValue("TCPNoDelay"));

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

    // --- build the old -> new diff with verification ---
    QVector<SettingChange> changes;

    auto makeChange = [&](const QString& key, const QString& section, const QString& name,
                          const QString& action, const QString& expectedNew) {
        SettingChange c;
        c.section = section.toStdString();
        c.name = name.toStdString();
        c.action = action.toStdString();
        c.old_value = before.value(key).toStdString();
        c.new_value = expectedNew.toStdString();
        QString after = [&]() -> QString {
            if (key == "GameDVR") return QString::fromStdString(SystemAudit::readGameDvrValue());
            if (key == "FullscreenOpt") return QString::fromStdString(SystemAudit::readFullscreenOptValue());
            if (key == "GameMode") return QString::fromStdString(SystemAudit::readGameModeValue());
            if (key == "PowerPlan") return QString::fromStdString(SystemAudit::readActivePowerPlan());
            if (key == "TcpAck") return QString::fromStdString(SystemAudit::readTcpValue("TcpAckFrequency"));
            if (key == "TcpNoDelay") return QString::fromStdString(SystemAudit::readTcpValue("TCPNoDelay"));
            return QString();
        }();
        if (after == before.value(key)) {
            c.status = SettingChange::Status::NotApplied;
            c.detail = "Значение не изменилось — параметр уже был применён ранее или не доступен.";
        } else if (!after.isEmpty()) {
            c.status = SettingChange::Status::Applied;
            c.new_value = after.toStdString();
            c.detail = "Программа записала значение в реестр и проверила его обратно — применилось.";
        } else {
            c.status = SettingChange::Status::AdminRequired;
            c.detail = "Программа не смогла прочитать значение обратно — возможно, нужны права администратора.";
        }
        changes.append(c);
    };

    if (settings.disable_game_dvr)
        makeChange("GameDVR", "FPS", "Запись игр (Game DVR)", "Отключить фоновую запись", "0");
    if (settings.disable_fullscreen_opt)
        makeChange("FullscreenOpt", "FPS", "Оптимизации полноэкранного режима", "Отключить", "1");
    if (settings.disable_game_mode)
        makeChange("GameMode", "FPS", "Игровой режим Windows", "Отключить", "0");
    if (settings.optimize_power_plan)
        makeChange("PowerPlan", "FPS", "План питания", "Высокая производительность", "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c");
    if (settings.tcp_optimization) {
        makeChange("TcpAck", "Network", "Адаптивные ACK (TcpAckFrequency)", "Включить", "1");
        makeChange("TcpNoDelay", "Network", "TCP NoDelay", "Включить", "1");
    }

    // verify everything again (authoritative read-back check)
    auto verifiedFps = SystemAudit::verifyFpsSettings();
    auto verifiedNet = SystemAudit::verifyNetworkSettings();
    for (auto& v : verifiedFps)
        if (v.status == SettingChange::Status::Verified || v.status == SettingChange::Status::Failed)
            changes.append(v);
    for (auto& v : verifiedNet)
        if (v.status == SettingChange::Status::Verified || v.status == SettingChange::Status::Failed)
            changes.append(v);

    renderChanges(changes);
    renderCapabilities();

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

QWidget* OptimizerWidget::makeChangeCard(const SettingChange& c)
{
    auto* card = new QWidget(this);
    card->setObjectName("gameCard");

    QString color;
    QString icon;
    QString statusText;
    switch (c.status) {
        case SettingChange::Status::Applied:
            color = theme::Colors::SUCCESS; icon = QString::fromUtf8("✓"); statusText = QString::fromUtf8("Применено"); break;
        case SettingChange::Status::Verified:
            color = theme::Colors::SUCCESS; icon = QString::fromUtf8("✓✓"); statusText = QString::fromUtf8("Проверено чтением реестра"); break;
        case SettingChange::Status::AdminRequired:
            color = theme::Colors::WARNING; icon = QString::fromUtf8("⚠"); statusText = QString::fromUtf8("Нужны права администратора"); break;
        case SettingChange::Status::Failed:
            color = theme::Colors::ERROR; icon = QString::fromUtf8("✕"); statusText = QString::fromUtf8("Не удалось"); break;
        default:
            color = theme::Colors::TEXT_TERTIARY; icon = QString::fromUtf8("·"); statusText = QString::fromUtf8("Не применялось"); break;
    }

    auto* row = new QHBoxLayout(card);
    row->setContentsMargins(12, 8, 12, 8);
    row->setSpacing(10);

    auto* iconLbl = new QLabel(icon, card);
    iconLbl->setStyleSheet(QString("color:%1; font-size:16px; font-weight:700; background:transparent;").arg(color));
    row->addWidget(iconLbl);

    auto* textLayout = new QVBoxLayout();
    auto* titleRow = new QHBoxLayout();
    auto* titleLbl = new QLabel(QString::fromStdString(c.name), card);
    titleLbl->setObjectName("gameTitle");
    titleRow->addWidget(titleLbl);
    titleRow->addStretch();
    auto* catLbl = new QLabel(QString::fromStdString(c.section), card);
    catLbl->setObjectName("gameCategory");
    titleRow->addWidget(catLbl);
    textLayout->addLayout(titleRow);

    auto* valsLbl = new QLabel(QString::fromUtf8("было: %1   →   стало: %2")
        .arg(QString::fromStdString(c.old_value.empty() ? "-" : c.old_value),
             QString::fromStdString(c.new_value.empty() ? "-" : c.new_value)), card);
    valsLbl->setStyleSheet(QString("color:%1; font-size:12px; background:transparent;").arg(color));
    textLayout->addWidget(valsLbl);

    auto* detailLbl = new QLabel(QString::fromStdString(c.detail), card);
    detailLbl->setStyleSheet("color:rgba(255,255,255,0.5); font-size:11px; font-style:italic; background:transparent;");
    detailLbl->setWordWrap(true);
    textLayout->addWidget(detailLbl);

    auto* statusLbl = new QLabel(statusText, card);
    statusLbl->setStyleSheet(QString("color:%1; font-size:11px; font-weight:600; background:transparent;").arg(color));
    textLayout->addWidget(statusLbl);

    row->addLayout(textLayout, 1);
    return card;
}

void OptimizerWidget::renderChanges(const QVector<SettingChange>& changes)
{
    auto* layout = qobject_cast<QVBoxLayout*>(changes_list_->layout());
    if (!layout) return;

    QLayoutItem* item;
    while ((item = layout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    if (changes.isEmpty()) {
        auto* lbl = new QLabel(QString::fromUtf8("Нажмите «Применить», чтобы увидеть разницу «до → после»."), this);
        lbl->setObjectName("sectionSubtitle");
        layout->addWidget(lbl);
        layout->addStretch();
        return;
    }

    for (const auto& c : changes)
        layout->addWidget(makeChangeCard(c));
    layout->addStretch();
}

QWidget* OptimizerWidget::makeCapabilityCard(const Capability& c)
{
    auto* card = new QWidget(this);
    card->setObjectName("gameCard");

    QColor color = c.currently_possible ? QColor(theme::Colors::SUCCESS)
                   : c.requires_vpn_server ? QColor(theme::Colors::TEXT_TERTIARY)
                                           : QColor(theme::Colors::WARNING);
    QString icon = c.currently_possible ? QString::fromUtf8("✓")
                  : c.requires_vpn_server ? QString::fromUtf8("⏳")
                                          : QString::fromUtf8("⚠");

    auto* row = new QHBoxLayout(card);
    row->setContentsMargins(12, 8, 12, 8);
    row->setSpacing(10);

    auto* iconLbl = new QLabel(icon, card);
    iconLbl->setStyleSheet(QString("color:%1; font-size:15px; font-weight:700; background:transparent;").arg(color.name()));
    row->addWidget(iconLbl);

    auto* textLayout = new QVBoxLayout();
    auto* titleLbl = new QLabel(QString::fromStdString(c.title), card);
    titleLbl->setObjectName("gameTitle");
    textLayout->addWidget(titleLbl);

    auto* descLbl = new QLabel(QString::fromStdString(c.description), card);
    descLbl->setObjectName("sectionSubtitle");
    descLbl->setWordWrap(true);
    textLayout->addWidget(descLbl);

    auto* seesLbl = new QLabel(QString::fromUtf8("Как программа это видит: ") +
                                   QString::fromStdString(c.what_it_sees), card);
    seesLbl->setStyleSheet("color:rgba(255,255,255,0.4); font-size:11px; font-style:italic; background:transparent;");
    seesLbl->setWordWrap(true);
    textLayout->addWidget(seesLbl);

    auto* statusLbl = new QLabel(QString::fromStdString(c.status_text), card);
    statusLbl->setStyleSheet(QString("color:%1; font-size:11px; font-weight:600; background:transparent;").arg(color.name()));
    textLayout->addWidget(statusLbl);

    row->addLayout(textLayout, 1);
    return card;
}

void OptimizerWidget::renderCapabilities()
{
    auto* layout = qobject_cast<QVBoxLayout*>(caps_list_->layout());
    if (!layout) return;

    QLayoutItem* item;
    while ((item = layout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    auto caps = SystemAudit::getCapabilities();
    for (const auto& c : caps)
        layout->addWidget(makeCapabilityCard(c));
    layout->addStretch();
}

} // namespace gno