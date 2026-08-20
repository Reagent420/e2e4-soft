#include "settings_page.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QFont>
#include <QPalette>

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
        show_notifications_ = createCheckBox(QString::fromUtf8("Показывать уведомления"), true, this);
        generalLayout->addWidget(start_windows_);
        generalLayout->addWidget(minimize_tray_);
        generalLayout->addWidget(show_notifications_);

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
        theme_ = createComboBox({QString::fromUtf8("Тёмная"), QString::fromUtf8("Светлая")}, 0, this);
        themeRow->addWidget(themeLabel);
        themeRow->addSpacing(12);
        themeRow->addWidget(theme_);
        themeRow->addStretch();
        generalLayout->addLayout(themeRow);

        connect(theme_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int index) { emit themeChanged(index == 1); });

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

        auto* appName = new QLabel(QString::fromUtf8("GNO — оптимизатор игровой сети"), this);
        QFont appFont = appName->font();
        appFont.setBold(true);
        appFont.setPointSize(13);
        appName->setFont(appFont);
        aboutLayout->addWidget(appName);

        aboutLayout->addWidget(new QLabel(QString::fromUtf8("Версия 1.1.0"), this));
        aboutLayout->addWidget(new QLabel(QString::fromUtf8("Собрано на Qt 6.11.1 + MinGW GCC 16.1.0"), this));
        aboutLayout->addWidget(new QLabel(QString::fromUtf8("Лицензия: MIT"), this));

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

void SettingsPageWidget::onResetDefaults() {
    start_windows_->setChecked(false);
    minimize_tray_->setChecked(true);
    show_notifications_->setChecked(true);
    language_->setCurrentIndex(0);
    theme_->setCurrentIndex(0);

    protocol_->setCurrentIndex(0);
    region_->setCurrentIndex(0);
    max_routes_->setCurrentIndex(2);
    ping_interval_->setCurrentIndex(1);

    verbose_log_->setChecked(false);
    auto_update_->setChecked(true);
    dev_mode_->setChecked(false);
}
