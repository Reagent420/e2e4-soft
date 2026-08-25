#include "settings_page.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QFont>
#include <QPalette>
#include <QSettings>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QCoreApplication>

#include "../core/pro_presets.h"
#include "theme.h"
#include "../core/i18n.h"

static QWidget* createSection(const QString& title, QVBoxLayout* contentLayout, QWidget* parent) {
    auto* group = new QWidget(parent);
    group->setObjectName("settingsGroup");
    group->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* layout = new QVBoxLayout(group);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(8);

    auto* titleLabel = new QLabel(title, group);
    titleLabel->setObjectName("sectionTitle");
    QFont titleFont = titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSize(11);
    titleLabel->setFont(titleFont);
    layout->addWidget(titleLabel);

    layout->addSpacing(4);
    layout->addLayout(contentLayout);

    return group;
}

static QComboBox* createComboBox(const QStringList& items, int currentIndex, QWidget* parent) {
    auto* combo = new QComboBox(parent);
    combo->addItems(items);
    combo->setCurrentIndex(currentIndex);
    combo->setMinimumWidth(180);
    return combo;
}

static QCheckBox* createCheckBox(const QString& text, bool checked, QWidget* parent) {
    auto* cb = new QCheckBox(text, parent);
    cb->setChecked(checked);
    return cb;
}

SettingsPageWidget::SettingsPageWidget(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    loadSettings();

    // wire signals that need to reach the rest of the app
    connect(theme_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) { emit themeChanged(index == 1); });
    connect(show_notifications_, &QCheckBox::toggled,
            this, &SettingsPageWidget::notificationsChanged);
    connect(sound_notifications_, &QCheckBox::toggled,
            this, &SettingsPageWidget::soundChanged);
    connect(overlay_enabled_, &QCheckBox::toggled,
            this, [this](bool on) {
        emit overlayChanged(on, overlay_corner_->currentIndex(), overlay_opacity_->currentIndex());
    });
    connect(overlay_corner_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        if (overlay_enabled_->isChecked())
            emit overlayChanged(true, overlay_corner_->currentIndex(), overlay_opacity_->currentIndex());
    });
    connect(overlay_opacity_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        if (overlay_enabled_->isChecked())
            emit overlayChanged(true, overlay_corner_->currentIndex(), overlay_opacity_->currentIndex());
    });
    connect(start_windows_, &QCheckBox::toggled,
            this, &SettingsPageWidget::applyStartWithWindows);

    // persist everything on any change
    auto saveAll = [this]() { saveSettings(); };
    for (QCheckBox* cb : {start_windows_, minimize_tray_, show_notifications_,
                          sound_notifications_, overlay_enabled_, verbose_log_,
                          auto_update_, dev_mode_}) {
        connect(cb, &QCheckBox::toggled, this, saveAll);
    }
    for (QComboBox* combo : {language_, theme_, protocol_, region_, max_routes_,
                             ping_interval_, overlay_corner_, overlay_opacity_}) {
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, saveAll);
    }
}

void SettingsPageWidget::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(32, 24, 32, 24);
    mainLayout->setSpacing(16);

    auto* headerLayout = new QHBoxLayout();
    auto* headerTitle = new QLabel(QString::fromUtf8("Настройки"), this);
    headerTitle->setObjectName("headerTitle");
    QFont hFont = headerTitle->font();
    hFont.setBold(true);
    hFont.setPointSize(16);
    headerTitle->setFont(hFont);

    auto* headerSubtitle = new QLabel(QString::fromUtf8("Настройка приложения"), this);
    headerSubtitle->setObjectName("headerSubtitle");

    headerLayout->addWidget(headerTitle);
    headerLayout->addSpacing(12);
    headerLayout->addWidget(headerSubtitle);
    headerLayout->addStretch();
    mainLayout->addLayout(headerLayout);

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setObjectName("settingsScrollArea");

    auto* scrollContent = new QWidget();
    auto* scrollLayout = new QVBoxLayout(scrollContent);
    scrollLayout->setContentsMargins(0, 0, 0, 0);
    scrollLayout->setSpacing(12);

    {
        auto* generalLayout = new QVBoxLayout();
        generalLayout->setSpacing(10);
        start_windows_ = createCheckBox(QString::fromUtf8("Запускать при загрузке Windows"), false, this);
        minimize_tray_ = createCheckBox(QString::fromUtf8("Сворачивать в системный трей"), true, this);
        show_notifications_ = createCheckBox(QString::fromUtf8("Показывать всплывающие уведомления"), true, this);
        sound_notifications_ = createCheckBox(QString::fromUtf8("Звуковые уведомления (старт/конец игры, рост пинга)"), true, this);
        overlay_enabled_ = createCheckBox(QString::fromUtf8("Игровой оверлей — пинг поверх игры (клавиша F9)"), false, this);
        generalLayout->addWidget(start_windows_);
        generalLayout->addWidget(minimize_tray_);
        generalLayout->addWidget(show_notifications_);
        generalLayout->addWidget(sound_notifications_);
        generalLayout->addWidget(overlay_enabled_);

        auto* langRow = new QHBoxLayout();
        auto* langLabel = new QLabel(QString::fromUtf8("Язык:"), this);
        language_ = createComboBox({QString::fromUtf8("Русский"), "English", "中文", "한국어", "日本語"}, 0, this);
        langRow->addWidget(langLabel);
        langRow->addSpacing(12);
        langRow->addWidget(language_);
        langRow->addStretch();
        generalLayout->addLayout(langRow);

        auto* themeRow = new QHBoxLayout();
        auto* themeLabel = new QLabel(QString::fromUtf8("Тема:"), this);
        theme_ = createComboBox({QString::fromUtf8("\xD0\xA1\xD0\xB2\xD0\xB5\xD1\x82\xD0\xBB\xD0\xB0\xD1\x8F"), QString::fromUtf8("\xD0\x9A\xD0\xB8\xD0\xB1\xD0\xB5\xD1\x80")}, 0, this);
        themeRow->addWidget(themeLabel);
        themeRow->addSpacing(12);
        themeRow->addWidget(theme_);

        auto* langLbl = new QLabel(QString::fromUtf8("\xD0\xAF%D0%B7%D1%8B%D0%BA\x3A"), this);
        themeRow->addSpacing(12);
        themeRow->addWidget(langLbl);
        language_ = createComboBox({QStringLiteral("Russian"), QStringLiteral("English")}, 
            QSettings().value(QStringLiteral("app/language"), 0).toInt(), this);
        themeRow->addWidget(language_);
        connect(language_, &QComboBox::currentIndexChanged, this, [this](int idx) {
            QSettings().setValue(QStringLiteral("app/language"), idx);
        });
        themeRow->addStretch();
        generalLayout->addLayout(themeRow);

        auto* cornerRow = new QHBoxLayout();
        auto* cornerLabel = new QLabel(QString::fromUtf8("Положение оверлея:"), this);
        overlay_corner_ = createComboBox({QString::fromUtf8("Верхний левый"), QString::fromUtf8("Верхний правый"),
                                          QString::fromUtf8("Нижний левый"), QString::fromUtf8("Нижний правый")}, 1, this);
        cornerRow->addWidget(cornerLabel);
        cornerRow->addSpacing(12);
        cornerRow->addWidget(overlay_corner_);
        cornerRow->addStretch();
        generalLayout->addLayout(cornerRow);

        auto* opacityRow = new QHBoxLayout();
        auto* opacityLabel = new QLabel(QString::fromUtf8("Прозрачность оверлея:"), this);
        overlay_opacity_ = createComboBox({"60%", "75%", "85%", "95%", "100%"}, 2, this);
        opacityRow->addWidget(opacityLabel);
        opacityRow->addSpacing(12);
        opacityRow->addWidget(overlay_opacity_);
        opacityRow->addStretch();
        generalLayout->addLayout(opacityRow);

        scrollLayout->addWidget(createSection(QString::fromUtf8("ОБЩИЕ"), generalLayout, this));
    }

    {
        auto* connLayout = new QVBoxLayout();
        connLayout->setSpacing(10);

        auto addComboRow = [&](const QString& labelText, QComboBox** combo, const QStringList& items, int defIdx) {
            auto* row = new QHBoxLayout();
            auto* label = new QLabel(labelText, this);
            *combo = createComboBox(items, defIdx, this);
            row->addWidget(label);
            row->addSpacing(12);
            row->addWidget(*combo);
            row->addStretch();
            connLayout->addLayout(row);
        };

        addComboRow(QString::fromUtf8("Протокол:"), &protocol_, {QString::fromUtf8("UDP"), QString::fromUtf8("TCP"), QString::fromUtf8("ICMP")}, 0);
        addComboRow(QString::fromUtf8("Регион серверов:"), &region_, {QString::fromUtf8("Автоопределение"), QString::fromUtf8("Европа"), QString::fromUtf8("Северная Америка"), QString::fromUtf8("Азия"), QString::fromUtf8("Южная Америка")}, 0);
        addComboRow(QString::fromUtf8("Макс. маршрутов:"), &max_routes_, {"1", "2", "3", "4", "5"}, 2);
        addComboRow(QString::fromUtf8("Интервал пинга:"), &ping_interval_, {"500", "1000", "2000", "5000"}, 1);

        scrollLayout->addWidget(createSection(QString::fromUtf8("ПОДКЛЮЧЕНИЕ"), connLayout, this));
    }

    {
        auto* proLayout = new QVBoxLayout();
        proLayout->setSpacing(10);

        auto* proRow = new QHBoxLayout();
        auto* proLabel = new QLabel(QString::fromUtf8("Игра:"), this);
        pro_game_combo_ = new QComboBox(this);
        pro_game_combo_->setMinimumWidth(220);
        proRow->addWidget(proLabel);
        proRow->addSpacing(12);
        proRow->addWidget(pro_game_combo_);
        proRow->addStretch();
        proLayout->addLayout(proRow);

        pro_desc_label_ = new QLabel(this);
        pro_desc_label_->setObjectName("sectionSubtitle");
        pro_desc_label_->setWordWrap(true);
        proLayout->addWidget(pro_desc_label_);

        auto* applyRow = new QHBoxLayout();
        auto* applyBtn = new QPushButton(QString::fromUtf8("Применить профиль"), this);
        applyBtn->setObjectName("boostButton");
        applyBtn->setFixedWidth(180);
        applyBtn->setFixedHeight(34);
        pro_status_label_ = new QLabel(this);
        pro_status_label_->setStyleSheet("color:rgba(255,255,255,0.5); font-size:11px; background:transparent;");
        pro_status_label_->setWordWrap(true);
        applyRow->addWidget(applyBtn);
        applyRow->addSpacing(12);
        applyRow->addWidget(pro_status_label_, 1);
        proLayout->addLayout(applyRow);

        scrollLayout->addWidget(createSection(QString::fromUtf8("ПРОФИЛИ КИБЕРСПОРТСМЕНОВ"), proLayout, this));

        auto presets = gno::ProPresets::allPresets();
        for (const auto& preset : presets)
            pro_game_combo_->addItem(QString::fromUtf8(preset.display_name.c_str()));

        auto updateProDesc = [this]() {
            auto presets = gno::ProPresets::allPresets();
            int idx = pro_game_combo_->currentIndex();
            if (idx >= 0 && idx < static_cast<int>(presets.size())) {
                const auto& preset = presets[idx];
                QString text = QString::fromUtf8(preset.description.c_str());
                if (preset.config_path.empty())
                    text += QString::fromUtf8("\n\n⚠ Игра не найдена — конфигурация будет применена после первого запуска игры.");
                else
                    text += QString::fromUtf8("\n\nФайл: %1").arg(QString::fromUtf8(preset.config_path.c_str()));
                pro_desc_label_->setText(text);
            }
        };
        connect(pro_game_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, updateProDesc);
        updateProDesc();

        connect(applyBtn, &QPushButton::clicked, this, [this]() {
            auto presets = gno::ProPresets::allPresets();
            int idx = pro_game_combo_->currentIndex();
            if (idx < 0 || idx >= static_cast<int>(presets.size()))
                return;
            std::string msg;
            bool ok = gno::ProPresets::applyPreset(presets[idx], msg);
            pro_status_label_->setText(QString::fromUtf8(msg.c_str()));
            pro_status_label_->setStyleSheet(
                ok
                    ? "color:#22c55e; font-size:11px; background:transparent;"
                    : "color:#f59e0b; font-size:11px; background:transparent;");
        });
    }

    {
        auto* advLayout = new QVBoxLayout();
        advLayout->setSpacing(10);
        verbose_log_ = createCheckBox(QString::fromUtf8("Подробные логи"), false, this);
        auto_update_ = createCheckBox(QString::fromUtf8("Автообновление"), true, this);
        dev_mode_ = createCheckBox(QString::fromUtf8("Режим разработчика"), false, this);
        advLayout->addWidget(verbose_log_);
        advLayout->addWidget(auto_update_);
        advLayout->addWidget(dev_mode_);

        scrollLayout->addWidget(createSection(QString::fromUtf8("ДОПОЛНИТЕЛЬНО"), advLayout, this));
    }

    {
        auto* aboutLayout = new QVBoxLayout();
        aboutLayout->setSpacing(6);

        auto* appName = new QLabel(QString::fromUtf8("E2E4 Soft — Оптимизатор игровой сети"), this);
        QFont appFont = appName->font();
        appFont.setBold(true);
        appFont.setPointSize(13);
        appName->setFont(appFont);
        aboutLayout->addWidget(appName);

        aboutLayout->addWidget(new QLabel(QString::fromUtf8("Версия %1").arg(gno::theme::APP_VERSION), this));
        aboutLayout->addWidget(new QLabel(QString::fromUtf8("Собрано на Qt 6.11.1 + MinGW GCC 16.1.0"), this));
        aboutLayout->addWidget(new QLabel(QString::fromUtf8("Лицензия: MIT"), this));

        auto* descLabel = new QLabel(
            QString::fromUtf8(
                "E2E4 Soft — программа для снижения пинга и повышения FPS в играх.\n"
                "Что умеет:\n"
                "• Мониторинг сети в реальном времени: пинг, джиттер, потери пакетов\n"
                "• Авто-обнаружение установленных игр (Steam / Epic / GOG)\n"
                "• Профили оптимизации для каждой игры с автоприменением\n"
                "• Ускорение FPS: отключение Game DVR, план питания, приоритет процесса\n"
                "• Спидтест и бенчмарк DNS-серверов с применением лучшего\n"
                "• Мультимаршрутное соединение и автовыбор маршрута\n"
                "• Монитор процессов: блокировка и завершение программ-пожирателей трафика\n"
                "• История сессий и карта серверов по всему миру\n"
                "• Игровой оверлей, сравнительные замеры «до/после» и экспорт отчётов PNG\n"
                "• Рекомендации на основе анализа ваших сессий"),
            this);
        descLabel->setObjectName("sectionSubtitle");
        descLabel->setWordWrap(true);
        aboutLayout->addWidget(descLabel);

        auto* githubLabel = new QLabel("GitHub: github.com/user/gno-native", this);
        QPalette pal = githubLabel->palette();
        pal.setColor(QPalette::WindowText, QColor(0x55, 0x99, 0xFF));
        githubLabel->setPalette(pal);
        aboutLayout->addWidget(githubLabel);

        scrollLayout->addWidget(createSection(QString::fromUtf8("О ПРОГРАММЕ"), aboutLayout, this));
    }

    scrollLayout->addStretch();
    scrollArea->setWidget(scrollContent);
    mainLayout->addWidget(scrollArea, 1);

    auto* btnRow = new QHBoxLayout();
    btnRow->setContentsMargins(0, 4, 0, 0);
    auto* resetBtn = new QPushButton(QString::fromUtf8("Сбросить настройки"), this);
    resetBtn->setObjectName("boostButton");
    resetBtn->setFixedWidth(200);
    btnRow->addWidget(resetBtn);
    btnRow->addStretch();
    mainLayout->addLayout(btnRow);

    connect(resetBtn, &QPushButton::clicked, this, &SettingsPageWidget::onResetDefaults);
}

void SettingsPageWidget::loadSettings() {
    QSettings settings;
    start_windows_->setChecked(settings.value("startWithWindows", false).toBool());
    minimize_tray_->setChecked(settings.value("minimizeToTray", true).toBool());
    show_notifications_->setChecked(settings.value("notifications", true).toBool());
    sound_notifications_->setChecked(settings.value("soundNotifications", true).toBool());
    overlay_enabled_->setChecked(settings.value("overlayEnabled", false).toBool());
    language_->setCurrentIndex(settings.value("language", 0).toInt());
    theme_->setCurrentIndex(settings.value("theme", "dark").toString() == "light" ? 1 : 0);
    protocol_->setCurrentIndex(settings.value("protocol", 0).toInt());
    region_->setCurrentIndex(settings.value("region", 0).toInt());
    max_routes_->setCurrentIndex(settings.value("maxRoutes", 2).toInt());
    ping_interval_->setCurrentIndex(settings.value("pingInterval", 1).toInt());
    overlay_corner_->setCurrentIndex(settings.value("overlayCorner", 1).toInt());
    overlay_opacity_->setCurrentIndex(settings.value("overlayOpacity", 2).toInt());
    verbose_log_->setChecked(settings.value("verboseLog", false).toBool());
    auto_update_->setChecked(settings.value("autoUpdate", true).toBool());
    dev_mode_->setChecked(settings.value("devMode", false).toBool());
}

void SettingsPageWidget::saveSettings() {
    QSettings settings;
    settings.setValue("startWithWindows", start_windows_->isChecked());
    settings.setValue("minimizeToTray", minimize_tray_->isChecked());
    settings.setValue("notifications", show_notifications_->isChecked());
    settings.setValue("soundNotifications", sound_notifications_->isChecked());
    settings.setValue("overlayEnabled", overlay_enabled_->isChecked());
    settings.setValue("language", language_->currentIndex());
    settings.setValue("theme", theme_->currentIndex() == 1 ? "light" : "dark");
    settings.setValue("protocol", protocol_->currentIndex());
    settings.setValue("region", region_->currentIndex());
    settings.setValue("maxRoutes", max_routes_->currentIndex());
    settings.setValue("pingInterval", ping_interval_->currentIndex());
    settings.setValue("overlayCorner", overlay_corner_->currentIndex());
    settings.setValue("overlayOpacity", overlay_opacity_->currentIndex());
    settings.setValue("verboseLog", verbose_log_->isChecked());
    settings.setValue("autoUpdate", auto_update_->isChecked());
    settings.setValue("devMode", dev_mode_->isChecked());
}

void SettingsPageWidget::applyStartWithWindows(bool enabled) {
#ifdef PLATFORM_WINDOWS
    QSettings reg("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    if (enabled) {
        QString exe = QCoreApplication::applicationFilePath();
        reg.setValue("E2E4Soft", "\"" + exe + "\"");
    } else {
        reg.remove("E2E4Soft");
    }
#else
    (void)enabled;
#endif
}

void SettingsPageWidget::onResetDefaults() {
    start_windows_->setChecked(false);
    minimize_tray_->setChecked(true);
    show_notifications_->setChecked(true);
    sound_notifications_->setChecked(true);
    overlay_enabled_->setChecked(false);
    language_->setCurrentIndex(0);
    theme_->setCurrentIndex(0);

    protocol_->setCurrentIndex(0);
    region_->setCurrentIndex(0);
    max_routes_->setCurrentIndex(2);
    ping_interval_->setCurrentIndex(1);
    overlay_corner_->setCurrentIndex(1);
    overlay_opacity_->setCurrentIndex(2);

    verbose_log_->setChecked(false);
    auto_update_->setChecked(true);
    dev_mode_->setChecked(false);

    saveSettings();
    emit themeChanged(false);
    emit overlayChanged(false, 1, 2);
    applyStartWithWindows(false);
}