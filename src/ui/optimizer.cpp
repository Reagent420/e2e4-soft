#include "optimizer.h"
#include "theme.h"

#include "../remediation/boost_gate.h"
#include "../remediation/remediation_service.h"
#include "../remediation/target_discovery.h"
#include "../core/settings_keys.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStringList>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <thread>

namespace gno {

namespace {

namespace rem = remediation;

constexpr const char* kPowerPlanHigh = "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c";

QString fromStd(const std::string& s) { return QString::fromStdString(s); }

QString backupsDirPath() {
    return fromStd(gno::remediation::defaultBackupsDir());
}

// В реестре лежат 0/1 и GUID — человеку это ничего не говорит.
// Значения приходят из SystemAudit, который читает их через движок, поэтому
// здесь уже человекочитаемые строки; переводим только «0/1» и GUID плана.
QString humanize(const QString& key, const QString& raw) {
    if (raw.isEmpty()) return QStringLiteral("не задано");
    if (key == QLatin1String("PowerPlan")) {
        if (raw == QLatin1String(kPowerPlanHigh)) return QStringLiteral("Высокая производительность");
        if (raw == QLatin1String("381b4222-f694-41f0-9685-ff5bb260df2e")) return QStringLiteral("Сбалансированная");
        if (raw == QLatin1String("a1841308-3541-4fab-bc81-f71556f20b4a")) return QStringLiteral("Экономия энергии");
        return raw;
    }
    if (raw == QLatin1String("0")) return QStringLiteral("отключено");
    if (raw == QLatin1String("1")) return QStringLiteral("включено");
    return raw;
}

// Разбор идентификатора действия из ответа движка.
// Раньше на неизвестной строке возвращался PowerPlan, и действие, которого UI
// не знает, показывалось пользователю как «План питания» — то есть как ДРУГОЕ
// действие. Теперь «не знаю» выражается явно (nullopt), и вызывающий
// показывает исходный id вместо чужого имени.
std::optional<rem::ActionId> actionIdFromString(const std::string& s) {
    for (int i = 0; i <= static_cast<int>(rem::ActionId::Cs2FpsUnlock); ++i) {
        const auto id = static_cast<rem::ActionId>(i);
        if (rem::to_string(id) == s) return id;
    }
    return std::nullopt;
}

// Человеческое имя действия; для незнакомого id — сам id, но не чужое имя.
QString actionLabel(const std::string& id) {
    const auto parsed = actionIdFromString(id);
    return parsed ? fromStd(rem::actionDisplayName(*parsed)) : fromStd(id);
}

QWidget* makeGroup(const QString& title, const QString& subtitle, QLayout* content) {
    auto* group = new QWidget;
    group->setObjectName("settingsGroup");

    auto* layout = new QVBoxLayout(group);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(10);

    auto* titleLabel = new QLabel(title);
    titleLabel->setObjectName("sectionTitle");
    layout->addWidget(titleLabel);

    if (!subtitle.isEmpty()) {
        auto* sub = new QLabel(subtitle);
        sub->setObjectName("sectionSubtitle");
        sub->setWordWrap(true);
        layout->addWidget(sub);
    }
    layout->addLayout(content);
    return group;
}

QScrollArea* makeScrollHost(QWidget* content) {
    auto* area = new QScrollArea;
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    area->setWidget(content);
    return area;
}

QCheckBox* addOptionRow(QGridLayout* grid, int row, const QString& text, const QString& desc,
                        bool checked) {
    auto* cb = new QCheckBox(text);
    cb->setChecked(checked);

    auto* descLabel = new QLabel(desc);
    descLabel->setWordWrap(true);
    descLabel->setStyleSheet(QString("color:%1; font-size:11px; background:transparent;")
                                 .arg(theme::Colors::TEXT_TERTIARY));

    grid->addWidget(cb, row, 0, Qt::AlignTop);
    grid->addWidget(descLabel, row, 1);
    return cb;
}

QLabel* makeHint(const QString& text) {
    auto* label = new QLabel(text);
    label->setWordWrap(true);
    label->setStyleSheet(QString("color:%1; font-size:11px; background:transparent;")
                             .arg(theme::Colors::TEXT_TERTIARY));
    return label;
}

// --- построение списка «до → после» --------------------------------------

struct ReadableAction {
    const char* id;
    const char* key;
    const char* section;
    const char* name;
    const char* action;
    const char* expected;
};

// Действия движка, для которых есть авторитетное чтение из реестра.
// TcpParameters разбирается отдельно: это одно значение (TcpInitialRtt).
//
// Ожидаемые значения обязаны совпадать с тем, что возвращает SystemAudit
// (он читает через движок). Иначе строка навсегда останется «не применилось».
// Строки для DNS/MTU/мыши собираем из констант движка, а не пишем руками.
const std::string kDnsExpected =
    std::string(rem::kEngineDnsPrimary) + ", " + rem::kEngineDnsSecondary;
const std::string kMtuExpected = std::to_string(rem::kEngineMtuValue) + " байт";
const std::string kTcpRttExpected = std::to_string(rem::kEngineTcpInitialRtt);

const ReadableAction kReadableActions[] = {
    {"GameDvr", "GameDVR", "FPS", "Запись игр (Game DVR)", "Отключить фоновую запись", "0"},
    {"FullscreenOptimizations", "FullscreenOpt", "FPS", "Оптимизации полноэкранного режима",
     "Отключить", kAuditOffText},
    {"GameMode", "GameMode", "FPS", "Игровой режим Windows", "Отключить", "0"},
    {"PowerPlan", "PowerPlan", "FPS", "План питания", "Высокая производительность", kPowerPlanHigh},
    {"MouseAccel", "MouseAccel", "FPS", "Ускорение мыши", "Отключить повышенную точность",
     kAuditOffText},
    {"Dns", "Dns", "Network", "DNS-серверы", "Быстрые резолверы", kDnsExpected.c_str()},
    {"Mtu", "Mtu", "Network", "MTU активного адаптера", "Убрать фрагментацию",
     kMtuExpected.c_str()},
};

// Раздел для действий без читаемого значения: раньше всё сваливалось в
// «Система», и сетевые пункты выглядели как системные.
const char* sectionForAction(const std::string& id) {
    if (id == "Dns" || id == "Mtu" || id == "TcpParameters") return "Network";
    if (id == "MouseAccel" || id == "Cs2FpsUnlock") return "FPS";
    if (id == "ProcessPriority" || id == "Cs2MaxPing") return "Игра";
    return "Система";
}

bool contains(const std::vector<std::string>& list, const char* value) {
    return std::find(list.begin(), list.end(), std::string(value)) != list.end();
}

QVector<SettingChange> buildChanges(const rem::BoostGateResult& gate,
                                    const QHash<QString, QString>& before,
                                    const QHash<QString, QString>& after) {
    QVector<SettingChange> out;

    auto add = [&](const char* section, const char* name, const char* action, const QString& key,
                   const QString& expected, bool applied) {
        SettingChange c;
        c.section = section;
        c.name = name;
        c.action = action;
        const QString b = before.value(key);
        const QString a = after.value(key);
        c.old_value = humanize(key, b).toStdString();

        if (!applied) {
            c.status = SettingChange::Status::Failed;
            c.new_value = humanize(key, b).toStdString();
            c.detail = "Не применилось — причина в блоке «Пропущено» ниже.";
        } else if (a.isEmpty()) {
            // Движок подтвердил запись, но UI не смог перечитать значение
            // (например, игра не запущена и читать нечего). Врать про «нужны
            // права администратора» нельзя — движок уже проверил запись сам.
            c.status = SettingChange::Status::Applied;
            c.new_value = humanize(key, expected).toStdString();
            c.detail = "Движок записал и проверил значение, но перечитать его из UI "
                       "не удалось (например, игра не запущена).";
        } else if (a == expected) {
            c.status = SettingChange::Status::Verified;
            c.new_value = humanize(key, a).toStdString();
            c.detail = (b == expected)
                           ? "Уже было настроено правильно — изменений не потребовалось."
                           : "Записано и проверено чтением из реестра — применилось.";
        } else {
            c.status = SettingChange::Status::Failed;
            c.new_value = humanize(key, a).toStdString();
            c.detail = QString::fromUtf8("Ожидалось «%1», в реестре «%2» — не применилось.")
                           .arg(humanize(key, expected), humanize(key, a))
                           .toStdString();
        }
        out.append(c);
    };

    for (const auto& d : kReadableActions) {
        // fromUtf8, а не fromLatin1: ожидаемые значения теперь человекочитаемые
        // («отключено», «1500 байт»), и fromLatin1 превращал их в мусор вида
        // «Ð¾Ñ‚ÐºÐ»ÑŽÑ‡ÐµÐ½Ð¾» — из-за этого сравнение не совпадало и строка
        // показывала «не применилось» сразу после успешного применения.
        const QString key = QString::fromUtf8(d.key);
        const QString expected = QString::fromUtf8(d.expected);
        if (contains(gate.succeeded, d.id))
            add(d.section, d.name, d.action, key, expected, true);
        else if (contains(gate.failed, d.id))
            add(d.section, d.name, d.action, key, expected, false);
    }

    // Движок настраивает TcpInitialRtt. Раньше здесь показывались
    // TcpAckFrequency/TCPNoDelay — параметры, которые движок не пишет вообще,
    // поэтому строки всегда выглядели как «не применилось».
    if (contains(gate.succeeded, "TcpParameters")) {
        add("Network", "Начальный RTT TCP (TcpInitialRtt)", "Ускорить установку соединения",
            QStringLiteral("TcpRtt"), QString::fromStdString(kTcpRttExpected), true);
    } else if (contains(gate.failed, "TcpParameters")) {
        add("Network", "Начальный RTT TCP (TcpInitialRtt)", "Ускорить установку соединения",
            QStringLiteral("TcpRtt"), QString::fromStdString(kTcpRttExpected), false);
    }

    // Действия без читаемого значения: показываем факт применения из ответа движка.
    auto known = [](const std::string& id) {
        for (const auto& d : kReadableActions)
            if (id == d.id) return true;
        return id == "TcpParameters";
    };
    auto addOutcomeOnly = [&](const std::vector<std::string>& list, bool applied) {
        for (const auto& id : list) {
            if (known(id)) continue;
            SettingChange c;
            c.section = sectionForAction(id);
            c.name = actionLabel(id).toStdString();
            c.action = applied ? "Применено безопасным движком" : "Не применилось";
            c.status = applied ? SettingChange::Status::Applied : SettingChange::Status::Failed;
            c.detail = applied
                           ? "Изменение сделано через движок: создан бэкап, откат доступен одной кнопкой."
                           : "Движок не смог применить действие — причина в блоке «Пропущено» ниже.";
            out.append(c);
        }
    };
    addOutcomeOnly(gate.succeeded, true);
    addOutcomeOnly(gate.failed, false);

    // Пропущенное движком — отдельными строками, чтобы не выдавать за успех.
    for (const auto& reason : gate.skipped) {
        SettingChange c;
        c.section = "Пропущено";
        c.name = reason;
        c.action = "Ничего не изменено";
        c.status = SettingChange::Status::NotApplied;
        c.detail = "Безопасный движок не берётся за это действие — система осталась как была.";
        out.append(c);
    }

    auto rank = [](SettingChange::Status s) {
        switch (s) {
            case SettingChange::Status::Failed: return 0;
            case SettingChange::Status::AdminRequired: return 1;
            case SettingChange::Status::Applied: return 2;
            case SettingChange::Status::Verified: return 2;
            case SettingChange::Status::NotApplied: return 3;
        }
        return 4;
    };
    std::stable_sort(out.begin(), out.end(), [&](const SettingChange& a, const SettingChange& b) {
        return rank(a.status) < rank(b.status);
    });
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// OptimizerWidget
// ---------------------------------------------------------------------------

OptimizerWidget::OptimizerWidget(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(12);

    buildHeader(root);
    buildControlPanel(root);
    buildTabs(root);

    loadSettings();
    refreshSelectionSummary();
    refreshRevertState();
    appendLog("Готово к работе. Отметьте нужные пункты и нажмите «Применить выбранное».");
    refreshReadinessAsync();

    // Фокус на поле MTU/DNS прокручивал список к середине — возвращаем начало.
    QTimer::singleShot(0, this, [this]() {
        if (profile_scroll_ && profile_scroll_->verticalScrollBar())
            profile_scroll_->verticalScrollBar()->setValue(0);
    });
}

void OptimizerWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (scroll_reset_done_) return;
    // Фокус назначается уже после показа, поэтому сбрасываем позицию следом.
    // Только один раз: дальше не мешаем пользователю листать список.
    QTimer::singleShot(0, this, [this]() {
        scroll_reset_done_ = true;
        if (profile_scroll_ && profile_scroll_->verticalScrollBar())
            profile_scroll_->verticalScrollBar()->setValue(0);
    });
}

// ---------------------------------------------------------------- header

void OptimizerWidget::buildHeader(QVBoxLayout* root) {
    auto* row = new QHBoxLayout;
    row->setSpacing(12);

    auto* textCol = new QVBoxLayout;
    textCol->setSpacing(2);

    auto* title = new QLabel(QString::fromUtf8("Оптимизация"));
    title->setStyleSheet(QString("font-size:20px; font-weight:700; color:%1; background:transparent;")
                             .arg(theme::Colors::TEXT_PRIMARY));
    auto* subtitle = new QLabel(
        QString::fromUtf8("Один профиль — одно нажатие. Каждое изменение проходит через безопасный "
                          "движок: резервная копия → запись → проверка чтением → откат."));
    subtitle->setWordWrap(true);
    subtitle->setStyleSheet(QString("color:%1; font-size:12px; background:transparent;")
                                .arg(theme::Colors::TEXT_TERTIARY));

    textCol->addWidget(title);
    textCol->addWidget(subtitle);
    row->addLayout(textCol, 1);

    admin_badge_ = new QLabel(QString::fromUtf8("…"));
    admin_badge_->setAlignment(Qt::AlignCenter);
    row->addWidget(admin_badge_, 0, Qt::AlignTop);

    root->addLayout(row);
}

// --------------------------------------------------------- control panel

void OptimizerWidget::buildControlPanel(QVBoxLayout* root) {
    auto* panel = new QWidget;
    panel->setObjectName("settingsGroup");

    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(10);

    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(10);

    apply_btn_ = new QPushButton(QString::fromUtf8("⚡  Применить выбранное"));
    apply_btn_->setObjectName("boostButton");
    apply_btn_->setFixedHeight(44);
    apply_btn_->setMinimumWidth(240);
    apply_btn_->setCursor(Qt::PointingHandCursor);

    revert_btn_ = new QPushButton(QString::fromUtf8("↩  Откатить последние изменения"));
    revert_btn_->setFixedHeight(44);
    revert_btn_->setCursor(Qt::PointingHandCursor);
    revert_btn_->setToolTip(QString::fromUtf8(
        "Возвращает все параметры к значениям, которые были до последнего применения."));

    reset_btn_ = new QPushButton(QString::fromUtf8("Сбросить выбор"));
    reset_btn_->setFixedHeight(44);
    reset_btn_->setCursor(Qt::PointingHandCursor);

    refresh_btn_ = new QPushButton(QString::fromUtf8("Обновить состояние"));
    refresh_btn_->setFixedHeight(44);
    refresh_btn_->setCursor(Qt::PointingHandCursor);

    buttons->addWidget(apply_btn_);
    buttons->addWidget(revert_btn_);
    buttons->addWidget(reset_btn_);
    buttons->addWidget(refresh_btn_);
    buttons->addStretch();
    layout->addLayout(buttons);

    progress_ = new QProgressBar;
    progress_->setRange(0, 0);
    progress_->setTextVisible(false);
    progress_->setFixedHeight(6);
    progress_->setVisible(false);
    layout->addWidget(progress_);

    status_label_ = new QLabel(QString::fromUtf8("Готово к применению"));
    status_label_->setWordWrap(true);
    layout->addWidget(status_label_);

    selection_label_ = new QLabel;
    selection_label_->setWordWrap(true);
    layout->addWidget(selection_label_);

    readiness_label_ = new QLabel(QString::fromUtf8("Проверяю, что доступно прямо сейчас…"));
    readiness_label_->setWordWrap(true);
    readiness_label_->setStyleSheet(QString("color:%1; font-size:11px; background:transparent;")
                                        .arg(theme::Colors::TEXT_TERTIARY));
    layout->addWidget(readiness_label_);

    root->addWidget(panel);
    setStatus(QString::fromUtf8("Готово к применению"), theme::Colors::TEXT_SECONDARY);

    connect(apply_btn_, &QPushButton::clicked, this, &OptimizerWidget::onApplyClicked);
    connect(revert_btn_, &QPushButton::clicked, this, &OptimizerWidget::onRevertClicked);
    connect(reset_btn_, &QPushButton::clicked, this, &OptimizerWidget::onResetClicked);
    connect(refresh_btn_, &QPushButton::clicked, this, &OptimizerWidget::onRefreshStateClicked);
}

// ----------------------------------------------------------------- tabs

void OptimizerWidget::buildTabs(QVBoxLayout* root) {
    tabs_ = new QTabWidget(this);
    tabs_->setDocumentMode(true);
    tabs_->addTab(buildProfileTab(), QString::fromUtf8("Профиль"));
    tabs_->addTab(buildChangesTab(), QString::fromUtf8("Что изменилось"));
    tabs_->addTab(buildLogTab(), QString::fromUtf8("Журнал"));
    root->addWidget(tabs_, 1);
}

QWidget* OptimizerWidget::buildProfileTab() {
    auto* content = new QWidget;
    auto* column = new QVBoxLayout(content);
    column->setContentsMargins(4, 4, 4, 4);
    column->setSpacing(12);

    // --- FPS ---
    auto* fpsGrid = new QGridLayout;
    fpsGrid->setHorizontalSpacing(14);
    fpsGrid->setVerticalSpacing(10);
    fpsGrid->setColumnStretch(1, 1);

    fps_checkboxes_[0] = addOptionRow(fpsGrid, 0,
        QString::fromUtf8("Отключить запись игр (Game DVR)"),
        QString::fromUtf8("Останавливает фоновую запись Windows, которая отнимает FPS"), true);
    fps_checkboxes_[1] = addOptionRow(fpsGrid, 1,
        QString::fromUtf8("Отключить оптимизации полноэкранного режима"),
        QString::fromUtf8("Убирает задержку ввода. Применится, когда запущена игра из профиля"), true);
    fps_checkboxes_[2] = addOptionRow(fpsGrid, 2,
        QString::fromUtf8("Отключить ускорение мыши"),
        QString::fromUtf8("Прямой ввод с мыши — точнее прицеливание в шутерах"), true);
    fps_checkboxes_[3] = addOptionRow(fpsGrid, 3,
        QString::fromUtf8("Отключить игровой режим Windows"),
        QString::fromUtf8("Иногда вызывает микролаги — отключаем для стабильности"), false);
    fps_checkboxes_[4] = addOptionRow(fpsGrid, 4,
        QString::fromUtf8("Максимальная производительность питания"),
        QString::fromUtf8("Переключает Windows на план «Высокая производительность»"), true);
    fps_checkboxes_[5] = addOptionRow(fpsGrid, 5,
        QString::fromUtf8("Высокий приоритет процесса игры"),
        QString::fromUtf8("Игра получает больше ресурсов CPU. Применится, когда игра запущена"), true);
    fps_checkboxes_[6] = addOptionRow(fpsGrid, 6,
        QString::fromUtf8("Оптимизация виртуальной памяти"),
        QString::fromUtf8("Вне белого списка движка: программа не будет менять этот параметр"), true);

    // Виртуальная память вне белого списка движка. Пункт оставляем видимым,
    // но не даём отметить — так же, как три сетевых пункта ниже. Раньше он был
    // отмечен по умолчанию и попадал в «Применить выбранное (N)», хотя движок
    // его всегда пропускал: каждое применение заканчивалось «Пропущено 1».
    fps_checkboxes_[6]->setChecked(false);
    fps_checkboxes_[6]->setEnabled(false);
    fps_checkboxes_[6]->setToolTip(QString::fromUtf8(
        "Вне белого списка движка: GNO этот параметр не меняет. "
        "Настраивается вручную: Свойства системы → Дополнительно → Быстродействие."));

    column->addWidget(makeGroup(QString::fromUtf8("УСКОРЕНИЕ FPS"),
                                QString::fromUtf8("Игровые параметры Windows, которые чаще всего "
                                                  "съедают кадры и добавляют задержку ввода."),
                                fpsGrid));

    // --- Network ---
    auto* netGrid = new QGridLayout;
    netGrid->setHorizontalSpacing(14);
    netGrid->setVerticalSpacing(10);
    netGrid->setColumnStretch(1, 1);

    net_checkboxes_[0] = addOptionRow(netGrid, 0,
        QString::fromUtf8("Мультимаршрутный режим"),
        QString::fromUtf8("Данные идут по нескольким путям — ниже пинг и меньше потерь"), true);
    net_checkboxes_[1] = addOptionRow(netGrid, 1,
        QString::fromUtf8("Автовыбор лучшего маршрута"),
        QString::fromUtf8("Программа сама переключается на самый быстрый путь"), true);
    net_checkboxes_[2] = addOptionRow(netGrid, 2,
        QString::fromUtf8("Компенсация потерь пакетов"),
        QString::fromUtf8("Отправляет дубликаты пакетов при нестабильном соединении"), true);
    net_checkboxes_[3] = addOptionRow(netGrid, 3,
        QString::fromUtf8("Свой DNS-сервер"),
        QString::fromUtf8("Быстрый DNS ускоряет подключение к игровым серверам"), false);
    net_checkboxes_[4] = addOptionRow(netGrid, 4,
        QString::fromUtf8("Оптимизация TCP-стека"),
        QString::fromUtf8("Адаптивные ACK и параметры TCP снижают задержку в играх"), true);
    net_checkboxes_[5] = addOptionRow(netGrid, 5,
        QString::fromUtf8("Оптимизация MTU"),
        QString::fromUtf8("Подбор размера пакета уменьшает фрагментацию и потери"), true);

    // Три первых пункта требуют серверной сети (подписка) — не делаем вид, что они работают.
    for (int i = 0; i < 3; ++i) {
        net_checkboxes_[i]->setEnabled(false);
        net_checkboxes_[i]->setToolTip(QString::fromUtf8(
            "Будет доступно после подключения серверной сети (подписка) — сейчас не применяется"));
    }

    auto* dnsRow = new QHBoxLayout;
    dnsRow->setContentsMargins(24, 0, 0, 0);
    auto* dnsLabel = new QLabel(QString::fromUtf8("DNS:"));
    dnsLabel->setStyleSheet(QString("color:%1; font-size:12px; background:transparent;")
                                .arg(theme::Colors::TEXT_SECONDARY));
    dns_input_ = new QLineEdit(QString::fromLatin1(rem::kEngineDnsPrimary));
    dns_input_->setObjectName("searchBox");
    dns_input_->setMaximumWidth(200);
    dns_input_->setEnabled(false);
    dnsRow->addWidget(dnsLabel);
    dnsRow->addWidget(dns_input_);
    dnsRow->addStretch();
    netGrid->addLayout(dnsRow, 6, 0, 1, 2);
    dns_hint_ = makeHint(QString());
    netGrid->addWidget(dns_hint_, 7, 0, 1, 2);

    auto* mtuRow = new QHBoxLayout;
    mtuRow->setContentsMargins(24, 0, 0, 0);
    auto* mtuLabel = new QLabel(QString::fromUtf8("MTU:"));
    mtuLabel->setStyleSheet(QString("color:%1; font-size:12px; background:transparent;")
                                .arg(theme::Colors::TEXT_SECONDARY));
    mtu_input_ = new QLineEdit(QString::number(rem::kEngineMtuValue));
    mtu_input_->setObjectName("searchBox");
    mtu_input_->setMaximumWidth(200);
    mtu_input_->setEnabled(false);
    mtuRow->addWidget(mtuLabel);
    mtuRow->addWidget(mtu_input_);
    mtuRow->addStretch();
    netGrid->addLayout(mtuRow, 8, 0, 1, 2);
    mtu_hint_ = makeHint(QString());
    netGrid->addWidget(mtu_hint_, 9, 0, 1, 2);

    column->addWidget(makeGroup(QString::fromUtf8("ОПТИМИЗАЦИЯ СЕТИ"),
                                QString::fromUtf8("Сетевые параметры применяются к активному "
                                                  "адаптеру с резервной копией прежних значений."),
                                netGrid));

    column->addStretch();

    for (auto* cb : fps_checkboxes_)
        connect(cb, &QCheckBox::toggled, this, &OptimizerWidget::onSelectionChanged);
    for (auto* cb : net_checkboxes_)
        connect(cb, &QCheckBox::toggled, this, &OptimizerWidget::onSelectionChanged);
    connect(net_checkboxes_[3], &QCheckBox::toggled, this, &OptimizerWidget::onDnsToggled);
    connect(net_checkboxes_[5], &QCheckBox::toggled, this, &OptimizerWidget::onMtuToggled);
    connect(dns_input_, &QLineEdit::textChanged, this, &OptimizerWidget::updateInputHints);
    connect(mtu_input_, &QLineEdit::textChanged, this, &OptimizerWidget::updateInputHints);

    updateInputHints();
    profile_scroll_ = makeScrollHost(content);
    return profile_scroll_;
}

QWidget* OptimizerWidget::buildChangesTab() {
    auto* content = new QWidget;
    auto* column = new QVBoxLayout(content);
    column->setContentsMargins(4, 4, 4, 4);
    column->setSpacing(8);

    changes_list_ = new QWidget;
    auto* listLayout = new QVBoxLayout(changes_list_);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(6);
    column->addWidget(changes_list_);
    column->addStretch();

    renderChanges({});
    return makeScrollHost(content);
}

QWidget* OptimizerWidget::buildLogTab() {
    log_view_ = new QTextEdit;
    log_view_->setReadOnly(true);
    log_view_->setPlaceholderText(QString::fromUtf8("Здесь появится история действий…"));
    log_view_->setStyleSheet(
        QString("QTextEdit { background-color:%1; color:%2; border:1px solid %3;"
                " border-radius:8px; padding:8px; font-size:12px; }")
            .arg(theme::Colors::BG_SURFACE, theme::Colors::TEXT_SECONDARY,
                 theme::Colors::BORDER));
    return log_view_;
}

// -------------------------------------------------------------- settings

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
    s.dns_server = dns_input_->text().trimmed();
    bool ok = false;
    const int mtu = mtu_input_->text().trimmed().toInt(&ok);
    s.mtu_value = (ok && mtu >= 576 && mtu <= 1500) ? mtu : static_cast<int>(rem::kEngineMtuValue);
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
    namespace sk = gno::SettingsKeys;
    FPSBoostSettings s;
    s.disable_game_dvr = settings.value(sk::GameDvr(), s.disable_game_dvr).toBool();
    s.disable_fullscreen_opt = settings.value(sk::FullscreenOpt(), s.disable_fullscreen_opt).toBool();
    s.disable_mouse_accel = settings.value(sk::MouseAccel(), s.disable_mouse_accel).toBool();
    s.disable_game_mode = settings.value(sk::GameMode(), s.disable_game_mode).toBool();
    s.optimize_power_plan = settings.value(sk::PowerPlan(), s.optimize_power_plan).toBool();
    s.set_high_priority = settings.value(sk::HighPriority(), s.set_high_priority).toBool();
    // Пункт отключён в UI (вне белого списка движка): галочку не
    // восстанавливаем, иначе он снова попадёт в счётчик выбранного.
    s.optimize_virtual_memory = false;
    s.multipath_routing = settings.value(sk::Multipath(), s.multipath_routing).toBool();
    s.real_time_route = settings.value(sk::RealTimeRoute(), s.real_time_route).toBool();
    s.packet_loss_compensation = settings.value(sk::LossCompensation(), s.packet_loss_compensation).toBool();
    s.custom_dns = settings.value(sk::CustomDns(), s.custom_dns).toBool();
    s.tcp_optimization = settings.value(sk::TcpOpt(), s.tcp_optimization).toBool();
    s.mtu_optimization = settings.value(sk::MtuOpt(), s.mtu_optimization).toBool();
    s.dns_server = settings.value(sk::DnsServer(), s.dns_server).toString();
    s.mtu_value = settings.value(sk::MtuValue(), s.mtu_value).toInt();
    if (s.mtu_value < 576 || s.mtu_value > 1500) s.mtu_value = static_cast<int>(rem::kEngineMtuValue);
    setSettings(s);
}

void OptimizerWidget::saveSettings() {
    QSettings settings;
    namespace sk = gno::SettingsKeys;
    const auto s = getSettings();
    settings.setValue(sk::GameDvr(), s.disable_game_dvr);
    settings.setValue(sk::FullscreenOpt(), s.disable_fullscreen_opt);
    settings.setValue(sk::MouseAccel(), s.disable_mouse_accel);
    settings.setValue(sk::GameMode(), s.disable_game_mode);
    settings.setValue(sk::PowerPlan(), s.optimize_power_plan);
    settings.setValue(sk::HighPriority(), s.set_high_priority);
    settings.setValue(sk::VirtualMemory(), s.optimize_virtual_memory);
    settings.setValue(sk::Multipath(), s.multipath_routing);
    settings.setValue(sk::RealTimeRoute(), s.real_time_route);
    settings.setValue(sk::LossCompensation(), s.packet_loss_compensation);
    settings.setValue(sk::CustomDns(), s.custom_dns);
    settings.setValue(sk::TcpOpt(), s.tcp_optimization);
    settings.setValue(sk::MtuOpt(), s.mtu_optimization);
    settings.setValue(sk::DnsServer(), s.dns_server);
    settings.setValue(sk::MtuValue(), s.mtu_value);
}

// ------------------------------------------------------------ UI helpers

void OptimizerWidget::setStatus(const QString& text, const QString& color) {
    status_label_->setText(text);
    status_label_->setStyleSheet(QString("color:%1; font-size:12px; font-weight:600; "
                                         "background:transparent;")
                                     .arg(color));
}

void OptimizerWidget::setRunningUi(bool running) {
    running_ = running;
    progress_->setVisible(running);
    revert_btn_->setEnabled(!running && revert_btn_->property("hasBoost").toBool());
    reset_btn_->setEnabled(!running);
    refresh_btn_->setEnabled(!running);
    if (running) {
        apply_btn_->setEnabled(false);
        apply_btn_->setText(QString::fromUtf8("⏳  Применяю…"));
    } else {
        // Текст и доступность кнопки считает refreshSelectionSummary(): раньше
        // здесь жёстко ставился текст без счётчика, и после применения/отката
        // кнопка теряла «(7)» и оставалась активной даже при пустом выборе.
        refreshSelectionSummary();
    }
}

void OptimizerWidget::appendLog(const QString& line) {
    if (!log_view_) return;
    log_view_->append(QString::fromUtf8("[%1] %2")
                          .arg(QTime::currentTime().toString("HH:mm:ss"), line));
}

int OptimizerWidget::countSelected() const {
    int n = 0;
    for (auto* cb : fps_checkboxes_)
        if (cb && cb->isEnabled() && cb->isChecked()) ++n;
    for (auto* cb : net_checkboxes_)
        if (cb && cb->isEnabled() && cb->isChecked()) ++n;
    return n;
}

QString OptimizerWidget::backupsDir() const { return backupsDirPath(); }

void OptimizerWidget::onSelectionChanged() {
    refreshSelectionSummary();
}

void OptimizerWidget::onDnsToggled(bool checked) {
    dns_input_->setEnabled(checked);
    updateInputHints();
}

void OptimizerWidget::onMtuToggled(bool checked) {
    mtu_input_->setEnabled(checked);
    updateInputHints();
}

void OptimizerWidget::updateInputHints() {
    if (!dns_hint_ || !mtu_hint_) return;

    const QString dns = dns_input_->text().trimmed();
    const bool dns_ok = (dns == QLatin1String(rem::kEngineDnsPrimary));
    dns_hint_->setText(
        dns_ok
            ? QString::fromUtf8("Будет записана пара %1 + %2 вместо серверов провайдера.")
                  .arg(QString::fromLatin1(rem::kEngineDnsPrimary),
                       QString::fromLatin1(rem::kEngineDnsSecondary))
            : QString::fromUtf8("«%1» не поддерживается: движок допускает только %2. "
                                "Другой адрес записан не будет.")
                  .arg(dns, QString::fromLatin1(rem::kEngineDnsPrimary)));
    dns_hint_->setStyleSheet(QString("color:%1; font-size:11px; background:transparent;")
                                 .arg(dns_ok ? theme::Colors::TEXT_TERTIARY
                                             : theme::Colors::WARNING));

    bool ok = false;
    const int mtu = mtu_input_->text().trimmed().toInt(&ok);
    const bool mtu_ok = ok && mtu == static_cast<int>(rem::kEngineMtuValue);
    mtu_hint_->setText(
        mtu_ok
            ? QString::fromUtf8("Будет выставлено 1500 — убирает фрагментацию пакетов.")
            : QString::fromUtf8("Будет применено 1500 (значение движка). Введённое «%1» "
                                "не используется.")
                  .arg(mtu_input_->text().trimmed()));
    mtu_hint_->setStyleSheet(QString("color:%1; font-size:11px; background:transparent;")
                                 .arg(mtu_ok ? theme::Colors::TEXT_TERTIARY
                                             : theme::Colors::WARNING));
}

void OptimizerWidget::refreshSelectionSummary() {
    QStringList willApply;
    auto addIf = [&](QCheckBox* cb) {
        if (cb && cb->isEnabled() && cb->isChecked()) willApply << cb->text();
    };
    for (auto* cb : fps_checkboxes_) addIf(cb);
    for (auto* cb : net_checkboxes_) addIf(cb);

    const int total = countSelected();
    const QString label = QString::fromUtf8("⚡  Применить выбранное (%1)").arg(total);
    apply_btn_->setText(label);
    // Ширина по тексту: иначе «(8)» обрезалось на узком окне.
    apply_btn_->setMinimumWidth(apply_btn_->fontMetrics().horizontalAdvance(label) + 56);
    apply_btn_->setEnabled(!running_ && total > 0);

    QString text;
    if (total == 0)
        text = QString::fromUtf8("Ничего не выбрано — отметьте хотя бы один пункт.");
    else
        text = QString::fromUtf8("Будет применено: %1").arg(willApply.join(QString::fromUtf8(", ")));

    if (total > 0 && (!game_running_ || !interface_found_)) {
        QStringList limits;
        if (!game_running_)
            limits << QString::fromUtf8("пункты для игры применятся, когда игра запущена");
        if (!interface_found_)
            limits << QString::fromUtf8("сетевые пункты недоступны: адаптер не найден");
        text += QString::fromUtf8("  ·  ") + limits.join(QString::fromUtf8("; "));
    }
    if (total > 0 && !is_admin_)
        text += QString::fromUtf8("  ·  без прав администратора часть изменений не применится");

    selection_label_->setText(text);
    selection_label_->setStyleSheet(
        QString("color:%1; font-size:12px; background:transparent;")
            .arg(total == 0 ? theme::Colors::WARNING : theme::Colors::TEXT_SECONDARY));
}

void OptimizerWidget::refreshReadiness() {
    if (is_admin_) {
        admin_badge_->setText(QString::fromUtf8("✓ Права администратора"));
        admin_badge_->setStyleSheet(
            QString("color:%1; font-size:11px; font-weight:600; background:transparent;")
                .arg(theme::Colors::SUCCESS));
        // Подсказку про нехватку прав обязательно снимаем: раньше она ставилась
        // только в ветке «без прав» и оставалась висеть на бейдже «✓ Права
        // администратора» после повышения прав.
        admin_badge_->setToolTip(QString());
    } else {
        admin_badge_->setText(QString::fromUtf8("⚠ Без прав администратора"));
        admin_badge_->setStyleSheet(
            QString("color:%1; font-size:11px; font-weight:600; background:transparent;")
                .arg(theme::Colors::WARNING));
        admin_badge_->setToolTip(QString::fromUtf8(
            "Часть параметров (запись игр, план питания) требует прав администратора. "
            "Запустите программу от имени администратора."));
    }

    QStringList parts;
    parts << (interface_found_ ? QString::fromUtf8("сетевой адаптер найден")
                               : QString::fromUtf8("сетевой адаптер не найден"));
    parts << (game_running_ ? QString::fromUtf8("игра запущена")
                            : QString::fromUtf8("игра не запущена"));
    readiness_label_->setText(QString::fromUtf8("Сейчас: ") + parts.join(QString::fromUtf8(" · ")));
}

void OptimizerWidget::refreshReadinessAsync() {
    QPointer<OptimizerWidget> self(this);
    std::thread([self]() {
        const bool game = rem::discoverRunningGameProcess().has_value();
        const bool iface = rem::discoverPrimaryInterface().has_value();
        const bool admin = SystemAudit::isAdmin();
        if (!self) return;
        QMetaObject::invokeMethod(
            self,
            [self, game, iface, admin]() {
                if (!self) return;
                self->game_running_ = game;
                self->interface_found_ = iface;
                self->is_admin_ = admin;
                self->refreshReadiness();
                self->refreshSelectionSummary();
            },
            Qt::QueuedConnection);
    }).detach();
}

void OptimizerWidget::onRefreshStateClicked() {
    readiness_label_->setText(QString::fromUtf8("Проверяю, что доступно прямо сейчас…"));
    refreshReadinessAsync();
    appendLog(QString::fromUtf8("Обновлено состояние системы."));
}

void OptimizerWidget::refreshRevertState() {
    rem::BoostGate gate(backupsDir().toStdString());
    // Не только буст: одиночный фикс из «Диагностики» тоже откатывается, иначе
    // кнопка оставалась бы серой при реально применённых изменениях.
    const bool has = gate.hasRevertableChanges();
    revert_btn_->setProperty("hasBoost", has);
    revert_btn_->setEnabled(has && !running_);
    revert_btn_->setToolTip(
        has ? QString::fromUtf8("Вернуть параметры к значениям до последнего применения.")
            : QString::fromUtf8("Откатывать нечего: активных изменений от этой программы нет."));
}

void OptimizerWidget::notifyStateChanged() {
    emit systemStateChanged();
}

void OptimizerWidget::refreshExternalState() {
    // Журнал изменился извне: пересчитываем только то, что от него зависит.
    // Полный refreshReadinessAsync() здесь не нужен — он опрашивает систему в
    // отдельном потоке, а доступность отката от системы не зависит.
    refreshRevertState();
}

void OptimizerWidget::onResetClicked() {
    setSettings(FPSBoostSettings{});
    saveSettings();
    refreshSelectionSummary();
    updateInputHints();
    appendLog(QString::fromUtf8("Выбор сброшен к профилю по умолчанию."));
}

// ------------------------------------------------------------------ apply

void OptimizerWidget::onApplyClicked() {
    if (running_) return;

    const auto settings = getSettings();
    const int selected = countSelected();
    if (selected == 0) {
        setStatus(QString::fromUtf8("Ничего не выбрано — отметьте хотя бы один пункт."),
                  theme::Colors::WARNING);
        return;
    }

    saveSettings();
    setRunningUi(true);
    setStatus(QString::fromUtf8("Применяю %1 настроек: бэкап → запись → проверка чтением…")
                  .arg(selected),
              theme::Colors::TEXT_SECONDARY);
    appendLog(QString::fromUtf8("Запущено применение (%1 пунктов).").arg(selected));
    tabs_->setCurrentIndex(1);

    QPointer<OptimizerWidget> self(this);
    std::thread([self, settings]() {
        // BEFORE: что было в реестре до изменения. Один и тот же набор ключей
        // для before и after — иначе «стало» окажется пустым и строка соврёт.
        auto snapshot = []() {
            QHash<QString, QString> s;
            s["GameDVR"] = fromStd(SystemAudit::readGameDvrValue());
            s["FullscreenOpt"] = fromStd(SystemAudit::readFullscreenOptValue());
            s["GameMode"] = fromStd(SystemAudit::readGameModeValue());
            s["PowerPlan"] = fromStd(SystemAudit::readActivePowerPlan());
            s["TcpRtt"] = fromStd(SystemAudit::readTcpRttValue());
            s["MouseAccel"] = fromStd(SystemAudit::readMouseAccelValue());
            s["Dns"] = fromStd(SystemAudit::readDnsServersValue());
            s["Mtu"] = fromStd(SystemAudit::readMtuValue());
            return s;
        };
        const QHash<QString, QString> before = snapshot();

        rem::BoostRequest req;
        req.disable_game_dvr = settings.disable_game_dvr;
        req.disable_fullscreen_optimizations = settings.disable_fullscreen_opt;
        req.disable_mouse_acceleration = settings.disable_mouse_accel;
        req.disable_game_mode = settings.disable_game_mode;
        req.optimize_power_plan = settings.optimize_power_plan;
        req.set_high_priority = settings.set_high_priority;
        req.optimize_virtual_memory = settings.optimize_virtual_memory;
        req.custom_dns = settings.custom_dns;
        req.dns_server = settings.dns_server.toStdString();
        req.tcp_optimization = settings.tcp_optimization;
        req.mtu_optimization = settings.mtu_optimization;
        req.mtu_value = static_cast<unsigned>(settings.mtu_value);

        rem::BoostGateResult result;
        try {
            rem::BoostGate gate(backupsDirPath().toStdString());
            result = gate.applyBoost(req);
        } catch (...) {
            result.ok = false;
            result.detail = "внутренняя ошибка движка — изменения не применены";
        }

        const QHash<QString, QString> after = snapshot();

        const auto changes = buildChanges(result, before, after);

        if (!self) return;
        QMetaObject::invokeMethod(
            self,
            [self, result, changes]() {
                if (!self) return;
                self->setRunningUi(false);
                self->refreshSelectionSummary();
                self->renderChanges(changes);
                self->refreshRevertState();

                const int failed = static_cast<int>(result.failed.size());
                const int skipped = static_cast<int>(result.skipped.size());
                const int done = static_cast<int>(result.succeeded.size());

                if (result.applied) {
                    QString text = QString::fromUtf8("Готово: применено %1.").arg(done);
                    if (skipped > 0) text += QString::fromUtf8(" Пропущено %1.").arg(skipped);
                    if (failed > 0) text += QString::fromUtf8(" Не удалось %1.").arg(failed);
                    text += QString::fromUtf8(" Откат доступен кнопкой «Откатить последние изменения».");
                    self->setStatus(text, failed > 0 ? theme::Colors::WARNING
                                                     : theme::Colors::SUCCESS);
                } else if (result.error == rem::RemediationError::Busy) {
                    // Параллельная операция (например, из «Центра изменений»).
                    // Это не «не удалось применить» — применение вообще не
                    // начиналось, и система не тронута.
                    self->setStatus(
                        QString::fromUtf8("Другая операция уже выполняется. Дождитесь её "
                                          "завершения и нажмите «Применить выбранное» ещё раз."),
                        theme::Colors::WARNING);
                } else if (done > 0) {
                    // Раньше в этой ветке терялись упавшие действия: писалось
                    // только «остальное пропущено», хотя часть могла именно
                    // упасть — а это разные вещи и разные причины.
                    QString text = QString::fromUtf8("Применено %1").arg(done);
                    if (failed > 0) text += QString::fromUtf8(", не удалось %1").arg(failed);
                    if (skipped > 0) text += QString::fromUtf8(", пропущено %1").arg(skipped);
                    text += QString::fromUtf8(" — подробности во вкладке «Что изменилось».");
                    self->setStatus(text, theme::Colors::WARNING);
                } else {
                    QString detail = result.detail.empty()
                                         ? QString::fromUtf8("движок не применил ни одного действия")
                                         : fromStd(result.detail);
                    self->setStatus(QString::fromUtf8("Ничего не изменилось: %1").arg(detail),
                                    theme::Colors::WARNING);
                }

                self->appendLog(
                    QString::fromUtf8("Результат: применено %1, пропущено %2, ошибок %3.")
                        .arg(done)
                        .arg(skipped)
                        .arg(failed));
                // Журнал изменился — пусть соседняя вкладка перечитает его.
                if (result.applied || !result.succeeded.empty()) self->notifyStateChanged();

                for (const auto& s : result.skipped)
                    self->appendLog(QString::fromUtf8("пропущено — ") + fromStd(s));
                for (const auto& s : result.failed)
                    self->appendLog(QString::fromUtf8("не применилось — ") + actionLabel(s));
            },
            Qt::QueuedConnection);
    }).detach();
}

void OptimizerWidget::onRevertClicked() {
    if (running_) return;
    if (!revert_btn_->property("hasBoost").toBool()) {
        setStatus(QString::fromUtf8("Откатывать нечего: активных изменений нет."),
                  theme::Colors::TEXT_SECONDARY);
        return;
    }

    setRunningUi(true);
    setStatus(QString::fromUtf8("Откатываю последние изменения из резервной копии…"),
              theme::Colors::TEXT_SECONDARY);
    appendLog(QString::fromUtf8("Запущен откат последней транзакции."));

    QPointer<OptimizerWidget> self(this);
    std::thread([self]() {
        rem::BoostGateResult result;
        try {
            rem::BoostGate gate(backupsDirPath().toStdString());
            result = gate.revertBoost();
        } catch (...) {
            result.ok = false;
            result.detail = "внутренняя ошибка движка — откат не выполнен";
        }

        if (!self) return;
        QMetaObject::invokeMethod(
            self,
            [self, result]() {
                if (!self) return;
                self->setRunningUi(false);
                self->refreshRevertState();

                if (result.ok) {
                    // Список «было → стало» очищаем только при УСПЕШНОМ откате:
                    // система вернулась к прежним значениям, показывать нечего.
                    self->renderChanges({});
                    self->setStatus(
                        QString::fromUtf8("Откат выполнен: параметры вернулись к прежним значениям."),
                        theme::Colors::SUCCESS);
                    self->appendLog(QString::fromUtf8("Откат выполнен успешно."));
                    self->notifyStateChanged();
                } else {
                    // При неудаче список НЕ трогаем: именно в нём видно, что
                    // осталось применённым. Раньше он стирался и здесь, поэтому
                    // пользователь, прочитав «откат не удался», терял
                    // единственное место с текущим состоянием параметров.
                    if (result.error == rem::RemediationError::Busy) {
                        self->setStatus(
                            QString::fromUtf8("Другая операция уже выполняется — дождитесь её "
                                              "завершения и повторите откат."),
                            theme::Colors::WARNING);
                    } else {
                        self->setStatus(
                            QString::fromUtf8("Откат не удался: %1").arg(fromStd(result.detail)),
                            theme::Colors::WARNING);
                    }
                    self->appendLog(QString::fromUtf8("Ошибка отката: ") + fromStd(result.detail));
                }
            },
            Qt::QueuedConnection);
    }).detach();
}

// -------------------------------------------------------------- rendering

QWidget* OptimizerWidget::makeChangeCard(const SettingChange& c) {
    auto* card = new QWidget;
    card->setObjectName("gameCard");

    QString color;
    QString icon;
    QString statusText;
    switch (c.status) {
        case SettingChange::Status::Verified:
            color = theme::Colors::SUCCESS;
            icon = QString::fromUtf8("✓");
            statusText = QString::fromUtf8("Проверено чтением из реестра");
            break;
        case SettingChange::Status::Applied:
            color = theme::Colors::SUCCESS;
            icon = QString::fromUtf8("✓");
            statusText = QString::fromUtf8("Применено");
            break;
        case SettingChange::Status::AdminRequired:
            color = theme::Colors::WARNING;
            icon = QString::fromUtf8("⚠");
            statusText = QString::fromUtf8("Нужны права администратора");
            break;
        case SettingChange::Status::Failed:
            color = theme::Colors::DANGER;
            icon = QString::fromUtf8("✕");
            statusText = QString::fromUtf8("Не применилось");
            break;
        default:
            color = theme::Colors::TEXT_TERTIARY;
            icon = QString::fromUtf8("·");
            statusText = QString::fromUtf8("Не изменялось");
            break;
    }

    auto* row = new QHBoxLayout(card);
    row->setContentsMargins(12, 10, 12, 10);
    row->setSpacing(10);

    auto* stripe = new QFrame(card);
    stripe->setFixedWidth(4);
    stripe->setStyleSheet(QString("background:%1; border-radius:2px;").arg(color));
    row->addWidget(stripe);

    auto* iconLbl = new QLabel(icon, card);
    iconLbl->setStyleSheet(QString("color:%1; font-size:16px; font-weight:700; "
                                   "background:transparent;")
                               .arg(color));
    row->addWidget(iconLbl, 0, Qt::AlignTop);

    auto* textLayout = new QVBoxLayout;
    textLayout->setSpacing(3);

    auto* titleRow = new QHBoxLayout;
    auto* titleLbl = new QLabel(fromStd(c.name), card);
    titleLbl->setObjectName("gameTitle");
    titleLbl->setWordWrap(true);
    titleRow->addWidget(titleLbl, 1);
    auto* catLbl = new QLabel(fromStd(c.section), card);
    catLbl->setObjectName("gameCategory");
    titleRow->addWidget(catLbl, 0, Qt::AlignTop);
    textLayout->addLayout(titleRow);

    if (!c.old_value.empty() || !c.new_value.empty()) {
        auto* valsLbl = new QLabel(QString::fromUtf8("было: %1   →   стало: %2")
                                       .arg(fromStd(c.old_value.empty() ? "-" : c.old_value),
                                            fromStd(c.new_value.empty() ? "-" : c.new_value)),
                                   card);
        valsLbl->setStyleSheet(QString("color:%1; font-size:12px; background:transparent;")
                                   .arg(color));
        valsLbl->setWordWrap(true);
        textLayout->addWidget(valsLbl);
    }

    if (!c.detail.empty()) {
        auto* detailLbl = new QLabel(fromStd(c.detail), card);
        detailLbl->setStyleSheet(QString("color:%1; font-size:11px; background:transparent;")
                                     .arg(theme::Colors::TEXT_TERTIARY));
        detailLbl->setWordWrap(true);
        textLayout->addWidget(detailLbl);
    }

    auto* statusLbl = new QLabel(statusText, card);
    statusLbl->setStyleSheet(QString("color:%1; font-size:11px; font-weight:600; "
                                     "background:transparent;")
                                 .arg(color));
    textLayout->addWidget(statusLbl);

    row->addLayout(textLayout, 1);
    return card;
}

void OptimizerWidget::renderChanges(const QVector<SettingChange>& changes) {
    auto* layout = qobject_cast<QVBoxLayout*>(changes_list_->layout());
    if (!layout) return;

    QLayoutItem* item = nullptr;
    while ((item = layout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    if (changes.isEmpty()) {
        auto* lbl = new QLabel(
            QString::fromUtf8("Пока нечего показывать. Отметьте пункты во вкладке «Профиль» "
                              "и нажмите «Применить выбранное» — здесь появится таблица "
                              "«было → стало» с проверкой по реестру."),
            changes_list_);
        lbl->setObjectName("sectionSubtitle");
        lbl->setWordWrap(true);
        layout->addWidget(lbl);
        return;
    }

    for (const auto& c : changes)
        layout->addWidget(makeChangeCard(c));
}

} // namespace gno
