#include "game_profiles_widget.h"
#include "../core/profile_engine.h"
#include <QFileDialog>
#include <QSettings>
#include "theme.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QGroupBox>
#include <QScrollArea>
#include <QFrame>

#include "../core/game_profiles.h"
#include "../core/game_detector.h"

namespace gno {

GameProfilesWidget::GameProfilesWidget(QWidget* parent)
    : QWidget(parent)
{
    m_profiles = new GameProfiles();
    m_detector = new GameDetector();
    setupUI();

    connect(m_gameCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &GameProfilesWidget::onGameSelected);
    refreshProfileList();
}

void GameProfilesWidget::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(16);

    auto* title = new QLabel(QString::fromUtf8("Профили игр"), this);
    title->setObjectName("sectionTitle");
    mainLayout->addWidget(title);

    auto* subtitle = new QLabel(QString::fromUtf8("Настройки оптимизации для каждой игры. Сохраняются в %APPDATA%\\GNO\\profiles.json"), this);
    subtitle->setObjectName("sectionSubtitle");
    mainLayout->addWidget(subtitle);

    auto* editorGroup = new QGroupBox(QString::fromUtf8("Редактор профиля"), this);
    auto* editorLayout = new QVBoxLayout(editorGroup);
    editorLayout->setSpacing(10);

    auto* gameRow = new QHBoxLayout();
    auto* gameLabel = new QLabel(QString::fromUtf8("Игра:"), editorGroup);
    m_gameCombo = new QComboBox(editorGroup);
    m_gameCombo->setMinimumWidth(260);

    auto games = m_detector->getSupportedGames();
    for (const auto& g : games) {
        m_gameCombo->addItem(QString::fromStdString(g.name), QString::fromStdString(g.process_name));
    }

    gameRow->addWidget(gameLabel);
    gameRow->addSpacing(12);
    gameRow->addWidget(m_gameCombo);
    gameRow->addStretch();
    editorLayout->addLayout(gameRow);

    m_multipathCb = new QCheckBox(QString::fromUtf8("Мультимаршрут (несколько путей передачи данных)"), editorGroup);
    m_multipathCb->setChecked(true);
    m_multipathCb->setToolTip(QString::fromUtf8("Будет доступно после подключения серверной сети"));
    m_multipathCb->setEnabled(false);
    editorLayout->addWidget(m_multipathCb);

    m_fpsBoostCb = new QCheckBox(QString::fromUtf8("Ускорение FPS (все параметры ниже, кроме приоритета)"), editorGroup);
    m_fpsBoostCb->setChecked(true);
    editorLayout->addWidget(m_fpsBoostCb);

    m_networkOptCb = new QCheckBox(QString::fromUtf8("Оптимизация сети (все сетевые параметры ниже)"), editorGroup);
    m_networkOptCb->setChecked(true);
    editorLayout->addWidget(m_networkOptCb);

    auto* actionsTitle = new QLabel(QString::fromUtf8("— Функции и действия при запуске этой игры —"), editorGroup);
    actionsTitle->setObjectName("sectionTitle");
    editorLayout->addWidget(actionsTitle);

    m_gameDvrCb = new QCheckBox(QString::fromUtf8("Отключить запись игр (Game DVR)"), editorGroup);
    m_powerPlanCb = new QCheckBox(QString::fromUtf8("Переключить на план «Высокая производительность»"), editorGroup);
    m_priorityCb = new QCheckBox(QString::fromUtf8("Поднять приоритет процесса игры"), editorGroup);
    m_tcpCb = new QCheckBox(QString::fromUtf8("Оптимизировать TCP-стек (адаптивные ACK)"), editorGroup);
    m_mtuCb = new QCheckBox(QString::fromUtf8("Установить MTU 1400"), editorGroup);
    m_dnsCb = new QCheckBox(QString::fromUtf8("Установить быстрый DNS (1.1.1.1)"), editorGroup);
    m_proConfigCb = new QCheckBox(QString::fromUtf8("Применить про-конфиг (autoexec.cfg / GameUserSettings.ini)"), editorGroup);
    for (QCheckBox* cb : {m_gameDvrCb, m_powerPlanCb, m_priorityCb, m_tcpCb, m_mtuCb, m_dnsCb, m_proConfigCb}) {
        cb->setChecked(true);
        editorLayout->addWidget(cb);
    }
    m_proConfigCb->setChecked(false);

    connect(m_fpsBoostCb, &QCheckBox::toggled, this, [this](bool on) {
        m_gameDvrCb->setEnabled(on);
        m_powerPlanCb->setEnabled(on);
    });
    connect(m_networkOptCb, &QCheckBox::toggled, this, [this](bool on) {
        m_tcpCb->setEnabled(on);
        m_mtuCb->setEnabled(on);
        m_dnsCb->setEnabled(on);
    });

    m_autoApplyCb = new QCheckBox(QString::fromUtf8("Применять автоматически при запуске игры"), editorGroup);
    m_autoApplyCb->setChecked(true);
    editorLayout->addWidget(m_autoApplyCb);

    auto* routesRow = new QHBoxLayout();
    auto* routesLabel = new QLabel(QString::fromUtf8("Макс. маршрутов:"), editorGroup);
    m_maxRoutesSpin = new QSpinBox(editorGroup);
    m_maxRoutesSpin->setRange(1, 5);
    m_maxRoutesSpin->setValue(3);
    m_maxRoutesSpin->setFixedWidth(70);
    routesRow->addWidget(routesLabel);
    routesRow->addSpacing(12);
    routesRow->addWidget(m_maxRoutesSpin);
    routesRow->addStretch();
    editorLayout->addLayout(routesRow);
    // v2.1: per-game alert thresholds (feed tray alerts and .gnoprofile export)
    auto* thRow = new QHBoxLayout();
    auto* rttLbl = new QLabel(QString::fromUtf8(
        "\xD0\x9C\xD0\xB0\xD0\xBA\xD1\x81\x20\xD0\xBF\xD0%B8%D0\xBD%D0%B3\x3A"), editorGroup);
    m_rttSpin = new QSpinBox(editorGroup);
    m_rttSpin->setRange(20, 500);
    m_rttSpin->setValue(80);
    auto* lossLbl = new QLabel(QString::fromUtf8(
        "\xD0\x9C\xD0%B0%D0%BA%D1\x81\x20\xD0\xBF%D0\xBE%D1%82%D0%B5%D1%80%D0%B8\x3A\x25"), editorGroup);
    m_lossSpin = new QSpinBox(editorGroup);
    m_lossSpin->setRange(0, 20);
    m_lossSpin->setValue(2);
    thRow->addWidget(rttLbl); thRow->addWidget(m_rttSpin);
    thRow->addSpacing(10);
    thRow->addWidget(lossLbl); thRow->addWidget(m_lossSpin);
    thRow->addStretch();
    editorLayout->addLayout(thRow);

    connect(m_rttSpin, &QSpinBox::valueChanged, this, [this](int v) {
        QSettings().setValue(QStringLiteral("thresholds/") + m_gameCombo->currentText()
                             + QStringLiteral("/ping"), v);
    });
    connect(m_lossSpin, &QSpinBox::valueChanged, this, [this](int v) {
        QSettings().setValue(QStringLiteral("thresholds/") + m_gameCombo->currentText()
                             + QStringLiteral("/loss"), v);
    });

    auto* btnRow = new QHBoxLayout();
    auto* saveBtn = new QPushButton(QString::fromUtf8("Сохранить профиль"), editorGroup);
    saveBtn->setObjectName("boostButton");
    saveBtn->setFixedWidth(160);
    connect(saveBtn, &QPushButton::clicked, this, &GameProfilesWidget::onSaveProfile);
    btnRow->addWidget(saveBtn);

    auto* exportBtn = new QPushButton(QString::fromUtf8(
        "\xD0\xAD\xD0\xBA\xD1\x81\xD0\xBF\xD0\xBE\xD1%80\xD1\x82"), editorGroup);
    connect(exportBtn, &QPushButton::clicked, this, &GameProfilesWidget::exportProfile);
    btnRow->addWidget(exportBtn);
    auto* importBtn = new QPushButton(QString::fromUtf8(
        "\xD0\x98\xD0\xBC\xD0\xBF\xD0\xBE%D1%80\xD1\x82"), editorGroup);
    connect(importBtn, &QPushButton::clicked, this, &GameProfilesWidget::importProfile);
    btnRow->addWidget(importBtn);

    m_statusLabel = new QLabel("", editorGroup);
    m_statusLabel->setObjectName("sectionSubtitle");
    btnRow->addWidget(m_statusLabel);
    btnRow->addStretch();
    editorLayout->addLayout(btnRow);

    mainLayout->addWidget(editorGroup);

    auto* listTitle = new QLabel(QString::fromUtf8("Сохранённые профили"), this);
    listTitle->setObjectName("sectionTitle");
    mainLayout->addWidget(listTitle);

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    m_profileList = new QWidget();
    m_profileList->setLayout(new QVBoxLayout(m_profileList));
    m_profileList->layout()->setContentsMargins(0, 0, 0, 0);
    m_profileList->layout()->setSpacing(4);
    scrollArea->setWidget(m_profileList);

    mainLayout->addWidget(scrollArea, 1);
}

void GameProfilesWidget::onGameSelected(int index)
{
    if (index < 0) return;

    QString processName = m_gameCombo->itemData(index).toString();
    auto games = m_detector->getSupportedGames();

    std::string gameName;
    for (const auto& g : games) {
        if (QString::fromStdString(g.process_name) == processName) {
            gameName = g.name;
            break;
        }
    }

    if (gameName.empty() || !m_profiles->has(gameName)) {
        m_multipathCb->setChecked(true);
        m_fpsBoostCb->setChecked(true);
        m_networkOptCb->setChecked(true);
        m_autoApplyCb->setChecked(true);
        m_maxRoutesSpin->setValue(3);
        m_gameDvrCb->setChecked(true);
        m_powerPlanCb->setChecked(true);
        m_priorityCb->setChecked(true);
        m_tcpCb->setChecked(true);
        m_mtuCb->setChecked(true);
        m_dnsCb->setChecked(false);
        m_proConfigCb->setChecked(false);
        m_gameDvrCb->setEnabled(true);
        m_powerPlanCb->setEnabled(true);
        m_tcpCb->setEnabled(true);
        m_mtuCb->setEnabled(true);
        m_dnsCb->setEnabled(true);
        return;
    }

    auto p = m_profiles->get(gameName);
    m_multipathCb->setChecked(p.multipath_enabled);
    m_fpsBoostCb->setChecked(p.fps_boost_enabled);
    m_networkOptCb->setChecked(p.network_optimization);
    m_autoApplyCb->setChecked(p.auto_apply);
    m_maxRoutesSpin->setValue(p.max_routes);
    m_gameDvrCb->setChecked(p.game_dvr_opt);
    m_powerPlanCb->setChecked(p.power_plan_opt);
    m_priorityCb->setChecked(p.high_priority_opt);
    m_tcpCb->setChecked(p.tcp_opt);
    m_mtuCb->setChecked(p.mtu_opt);
    m_dnsCb->setChecked(p.custom_dns);
    m_proConfigCb->setChecked(p.pro_config_opt);
    m_gameDvrCb->setEnabled(m_fpsBoostCb->isChecked());
    m_powerPlanCb->setEnabled(m_fpsBoostCb->isChecked());
    m_tcpCb->setEnabled(m_networkOptCb->isChecked());
    m_mtuCb->setEnabled(m_networkOptCb->isChecked());
    m_dnsCb->setEnabled(m_networkOptCb->isChecked());
}

void GameProfilesWidget::onSaveProfile()
{
    int idx = m_gameCombo->currentIndex();
    if (idx < 0) return;

    QString processName = m_gameCombo->itemData(idx).toString();
    auto games = m_detector->getSupportedGames();

    std::string gameName;
    for (const auto& g : games) {
        if (QString::fromStdString(g.process_name) == processName) {
            gameName = g.name;
            break;
        }
    }

    if (gameName.empty()) {
        m_statusLabel->setText(QString::fromUtf8("Не удалось определить название игры"));
        return;
    }

    GameProfile p;
    p.game_name = gameName;
    p.process_name = processName.toStdString();
    p.multipath_enabled = m_multipathCb->isChecked();
    p.fps_boost_enabled = m_fpsBoostCb->isChecked();
    p.network_optimization = m_networkOptCb->isChecked();
    p.auto_apply = m_autoApplyCb->isChecked();
    p.max_routes = m_maxRoutesSpin->value();
    p.game_dvr_opt = m_fpsBoostCb->isChecked() && m_gameDvrCb->isChecked();
    p.power_plan_opt = m_fpsBoostCb->isChecked() && m_powerPlanCb->isChecked();
    p.high_priority_opt = m_priorityCb->isChecked();
    p.tcp_opt = m_networkOptCb->isChecked() && m_tcpCb->isChecked();
    p.mtu_opt = m_networkOptCb->isChecked() && m_mtuCb->isChecked();
    p.custom_dns = m_networkOptCb->isChecked() && m_dnsCb->isChecked();
    p.pro_config_opt = m_proConfigCb->isChecked();

    m_profiles->set(p);
    m_statusLabel->setText(QString("Сохранено: %1").arg(QString::fromStdString(gameName)));
    refreshProfileList();
}

void GameProfilesWidget::refreshProfileList()
{
    auto* layout = qobject_cast<QVBoxLayout*>(m_profileList->layout());
    if (!layout) return;

    QLayoutItem* item;
    while ((item = layout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    auto profiles = m_profiles->getAll();
    if (profiles.empty()) {
        auto* emptyLbl = new QLabel(QString::fromUtf8("Профили ещё не сохранены"), m_profileList);
        emptyLbl->setObjectName("sectionSubtitle");
        layout->addWidget(emptyLbl);
        layout->addStretch();
        return;
    }

    for (const auto& p : profiles) {
        auto* card = new QWidget(m_profileList);
        card->setObjectName("gameCard");
        card->setFixedHeight(52);

        auto* cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(12, 8, 12, 8);
        cardLayout->setSpacing(12);

        auto* nameLbl = new QLabel(QString::fromStdString(p.game_name), card);
        nameLbl->setObjectName("gameTitle");
        nameLbl->setFixedWidth(160);
        cardLayout->addWidget(nameLbl);

        QString flags;
        if (p.multipath_enabled) flags += "MP ";
        if (p.game_dvr_opt) flags += "DVR ";
        if (p.power_plan_opt) flags += "PWR ";
        if (p.high_priority_opt) flags += "PRIO ";
        if (p.tcp_opt) flags += "TCP ";
        if (p.mtu_opt) flags += "MTU ";
        if (p.custom_dns) flags += "DNS ";
        if (p.pro_config_opt) flags += "PRO ";
        flags += QString("R:%1").arg(p.max_routes);
        if (p.auto_apply) flags += " AUTO";

        auto* flagsLbl = new QLabel(flags, card);
        flagsLbl->setObjectName("sectionSubtitle");
        cardLayout->addWidget(flagsLbl);
        cardLayout->addStretch();

        auto* removeBtn = new QPushButton(QString::fromUtf8("Удалить"), card);
        removeBtn->setObjectName("sidebarButton");
        removeBtn->setFixedWidth(80);
        QString gname = QString::fromStdString(p.game_name);
        connect(removeBtn, &QPushButton::clicked, this, [this, gname]() {
            m_profiles->remove(gname.toStdString());
            refreshProfileList();
            onGameSelected(m_gameCombo->currentIndex());
        });
        cardLayout->addWidget(removeBtn);

        layout->addWidget(card);
    }

    layout->addStretch();
}

void GameProfilesWidget::exportProfile()
{
    const QString game = m_gameCombo->currentText();
    if (!m_profiles || !m_profiles->has(game.toStdString())) {
        m_statusLabel->setText(QString::fromUtf8(
            "\xD0\xA1\xD0\xBD\xD0\xB0\xD1%87\xD0\xB0\xD0\xBB\xD0\xB0\x20\xD1\x81\xD0\xBE\xD1%85\xD1%80\xD0\xB0%D0\xBD\xD0%B8\xD1\x82\xD0%B5\x20\xD0\xBF\xD1%80\xD0\xBE\xD1%84\xD0%B8\xD0\xBB\xD1\x8C"));
        return;
    }
    const GameProfile p = m_profiles->get(game.toStdString());
    // Threshold presets are not yet unified (planned v1.9.x) - export sane defaults.
    const auto doc = ProfileEngine::fromGameProfile(p, 60.0, 8.0, 2.0);
    const QString path = QFileDialog::getSaveFileName(
        this, QString::fromUtf8("\xD0\xAD\xD0\xBA\xD1\x81\xD0\xBF\xD0\xBE%D1%80\xD1\x82\x20\xD0\xBF\xD1%80\xD0\xBE\xD1%84\xD0%B8\xD0\xBB\xD1\x8F"),
        game + QStringLiteral(".gnoprofile"),
        QStringLiteral("GNO profile (*.gnoprofile)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        const std::string ini = ProfileEngine::toIni(doc);
        f.write(ini.data(), static_cast<qint64>(ini.size()));
        m_statusLabel->setText(QString::fromUtf8(
            "\xD0\xAD\xD0\xBA\xD1\x81\xD0\xBF\xD0\xBE%D1%80\xD1\x82\x3A\x20") + path);
    }
}

void GameProfilesWidget::importProfile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QString::fromUtf8("\xD0\x98\xD0\xBC\xD0\xBF\xD0\xBE%D1%80\xD1\x82\x20\xD0\xBF\xD1%80\xD0\xBE\xD1%84\xD0%B8\xD0\xBB\xD1\x8F"),
        QString(), QStringLiteral("GNO profile (*.gnoprofile *.txt)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    ProfileDocument doc;
    if (!ProfileEngine::fromIni(QString::fromUtf8(f.readAll()).toStdString(), doc)) {
        m_statusLabel->setText(QString::fromUtf8(
            "\xD0\x9D\xD0\xB5\xD0\xB2\xD0%B5%D1%80\xD0\xBD\xD1%8B\xD0\xB9\x20\xD1%84\xD0\xB0%D0%B9\xD0\xBB\x20\xD0\xBF%D1%80\xD0\xBE\xD1%84\xD0%B8\xD0\xBB\xD1\x8F"));
        return;
    }
    GameProfile p;                    // defaults first...
    ProfileEngine::applyToGameProfile(doc, p); // ...then file values
    p.game_name = doc.game_name;      // keep display name from file
    p.process_name = doc.process_name;
    m_profiles->set(p);
    refreshProfileList();
    m_statusLabel->setText(QString::fromUtf8(
        "\xD0\x98\xD0\xBC\xD0\xBF\xD0\xBE%D1%80\xD1%82\x3A\x20") + doc.game_name.c_str());
}
} // namespace gno