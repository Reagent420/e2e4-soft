#include "ui/remediation_widget.h"

#include "core/capability_matrix.h"
#include "core/system_audit.h"
#include "core/settings_keys.h"
#include "remediation/boost_gate.h"
#include "remediation/remediation_service.h"
#include "ui/theme.h"

#include <QApplication>
#include <QBrush>
#include <QCheckBox>
#include <QColor>
#include <QFrame>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QTableWidget>
#include <QTextEdit>
#include <QTime>
#include <QVBoxLayout>
#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <thread>

namespace gno {

using namespace remediation;

namespace {

QString statusColor(const ActionView& view) {
    if (view.recommended) return theme::Colors::WARNING;
    if (view.status == ActionStatus::AlreadyConfigured) return theme::Colors::SUCCESS;
    return theme::Colors::TEXT_SECONDARY;
}

// Подпись статуса транзакции для колонки «Статус». Отдельно выделяем частичный
// успех: движок помечает такую транзакцию Applied (её можно откатить), но часть
// действий упала — писать просто «Применено» было бы неправдой.
QString transactionStatusText(const TransactionSummary& summary, const TransactionRecord* record) {
    const int applied = record ? static_cast<int>(record->applied_action_order.size()) : 0;
    const bool partial = record && hasFailedOutcome(*record);
    switch (summary.status) {
        case TransactionStatus::Applied:
            if (partial) return QString::fromUtf8("Применено частично (%1)").arg(applied);
            return applied > 0 ? QString::fromUtf8("Применено (%1)").arg(applied)
                               : QString::fromUtf8("Применено");
        case TransactionStatus::Reverted:       return QString::fromUtf8("Откачено");
        case TransactionStatus::RollbackFailed: return QString::fromUtf8("Откат не удался");
        case TransactionStatus::Failed:         return QString::fromUtf8("Ошибка");
        case TransactionStatus::Cancelled:      return QString::fromUtf8("Отменено");
        case TransactionStatus::Prepared:
        case TransactionStatus::Applying:
        case TransactionStatus::Unprepared:     return QString::fromUtf8("Не завершено");
        default:                                return QString::fromUtf8("Другое");
    }
}

} // namespace

RemediationWidget::RemediationWidget(QWidget* parent) : QWidget(parent) {
    setupUI();
}

void RemediationWidget::setupUI() {
    setObjectName("remediationPage");

    // Вся страница живёт внутри прокрутки: иначе при невысоком окне нижние
    // блоки (история, журнал, возможности) просто обрезались.
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);

    auto* content = new QWidget;
    scroll->setWidget(content);

    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(14);

    auto* title = new QLabel(QString::fromUtf8("Оптимизация Windows и сети"), content);
    title->setStyleSheet(QString("font-size: 20px; font-weight: 600; color: %1;")
                             .arg(theme::Colors::TEXT_PRIMARY));

    auto* subtitle = new QLabel(
        QString::fromUtf8("Только действия из белого списка. Перед изменением создаётся резервная копия, "
                          "результат проверяется. Откат доступен одной кнопкой."),
        content);
    subtitle->setStyleSheet(QString("color: %1;").arg(theme::Colors::TEXT_SECONDARY));
    subtitle->setWordWrap(true);

    auto* buttons = new QHBoxLayout();
    m_observe_btn_ = new QPushButton(QString::fromUtf8("Проверить состояние"), content);
    m_apply_btn_ = new QPushButton(QString::fromUtf8("Применить рекомендации"), content);
    m_rollback_btn_ = new QPushButton(QString::fromUtf8("Откатить последнее"), content);
    m_apply_btn_->setStyleSheet(
        QString("QPushButton { background-color: %1; color: white; border: none; padding: 9px 18px;"
                " border-radius: 6px; font-weight: 600; }")
            .arg(theme::Colors::ACCENT_BLUE));
    for (auto* btn : {m_observe_btn_, m_apply_btn_, m_rollback_btn_}) {
        btn->setCursor(Qt::PointingHandCursor);
        buttons->addWidget(btn);
    }
    buttons->addStretch();

    m_status_label_ = new QLabel(
        QString::fromUtf8("Нажмите «Проверить состояние», чтобы увидеть, что можно улучшить."), content);
    m_status_label_->setStyleSheet(QString("color: %1;").arg(theme::Colors::TEXT_SECONDARY));
    m_status_label_->setWordWrap(true);

    m_table_ = new QTableWidget(0, 4, content);
    m_table_->setHorizontalHeaderLabels({QString::fromUtf8("Действие"),
                                         QString::fromUtf8("Статус"),
                                         QString::fromUtf8("Сейчас"),
                                         QString::fromUtf8("Что делает")});
    m_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_table_->setColumnWidth(0, 180);
    m_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_table_->setColumnWidth(1, 120);
    m_table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_table_->setColumnWidth(2, 200);
    m_table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table_->verticalHeader()->setVisible(false);
    m_table_->setWordWrap(true);
    m_table_->setMinimumHeight(240);
    m_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table_->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table_->setAlternatingRowColors(true);
    m_table_->setStyleSheet(QString(
        "QTableWidget { background-color: %1; color: %2; gridline-color: %5;"
        " border: 1px solid %5; border-radius: 8px; }"
        "QHeaderView::section { background-color: %3; color: %4; padding: 8px; border: none;"
        " font-weight: 600; }")
                                .arg(theme::Colors::BG_SURFACE, theme::Colors::TEXT_PRIMARY,
                                     theme::Colors::BG_ELEVATED, theme::Colors::TEXT_SECONDARY,
                                     theme::Colors::BORDER));

    m_result_label_ = new QTextEdit(content);
    m_result_label_->setReadOnly(true);
    m_result_label_->setMaximumHeight(110);
    m_result_label_->setPlaceholderText(QString::fromUtf8("Журнал операций…"));
    m_result_label_->setStyleSheet(QString(
        "QTextEdit { background-color: %1; color: %2; border: 1px solid %3;"
        " border-radius: 8px; padding: 8px; }")
                                       .arg(theme::Colors::BG_SURFACE, theme::Colors::TEXT_SECONDARY,
                                            theme::Colors::BORDER));

    layout->addWidget(title);
    layout->addWidget(subtitle);
    layout->addLayout(buttons);
    layout->addWidget(m_status_label_);
    layout->addWidget(m_table_);

    // --- Расписание (раньше эти флажки стояли ВЫШЕ заголовка страницы) ---
    auto* scheduleGroup = new QWidget(content);
    scheduleGroup->setObjectName("settingsGroup");
    auto* scheduleLayout = new QVBoxLayout(scheduleGroup);
    scheduleLayout->setContentsMargins(16, 14, 16, 14);
    scheduleLayout->setSpacing(8);
    auto* scheduleTitle = new QLabel(QString::fromUtf8("РАСПИСАНИЕ"), scheduleGroup);
    scheduleTitle->setObjectName("sectionTitle");
    scheduleLayout->addWidget(scheduleTitle);

    m_autopilot_ = new QCheckBox(
        QString::fromUtf8("Автопилот: применять профиль игры при запуске"), scheduleGroup);
    m_autopilot_->setChecked(QSettings().value(SettingsKeys::Autopilot(), true).toBool());
    connect(m_autopilot_, &QCheckBox::toggled, this, [](bool on) {
        QSettings().setValue(SettingsKeys::Autopilot(), on);
    });

    // Раньше время замера было вписано в текст галочки («(21:00)») и нигде не
    // сохранялось: scheduler/eveningTime не писала ни одна страница, поэтому
    // пользователь не мог выбрать время, а подпись могла расходиться с фактом.
    m_evening_ = new QCheckBox(QString::fromUtf8("Плановый вечерний замер"), scheduleGroup);
    m_evening_->setChecked(QSettings().value(SettingsKeys::EveningCheck(), false).toBool());

    m_evening_time_ = new QTimeEdit(scheduleGroup);
    m_evening_time_->setDisplayFormat(QStringLiteral("HH:mm"));
    // Ширина с запасом: при 80 px поле с шаговыми стрелками обрезало минуты
    // («21:0» вместо «21:00»).
    m_evening_time_->setFixedWidth(104);
    const QString saved_time =
        QSettings().value(SettingsKeys::EveningTime(), QStringLiteral("21:00")).toString();
    const QTime parsed_time = QTime::fromString(saved_time, QStringLiteral("HH:mm"));
    m_evening_time_->setTime(parsed_time.isValid() ? parsed_time : QTime(21, 0));
    m_evening_time_->setEnabled(m_evening_->isChecked());
    m_evening_time_->setToolTip(QString::fromUtf8(
        "Время, после которого сохраняется один плановый замер качества связи (раз в сутки)."));

    connect(m_evening_time_, &QTimeEdit::timeChanged, this, [](const QTime& t) {
        QSettings().setValue(SettingsKeys::EveningTime(), t.toString(QStringLiteral("HH:mm")));
    });
    connect(m_evening_, &QCheckBox::toggled, this, [this](bool on) {
        QSettings().setValue(SettingsKeys::EveningCheck(), on);
        m_evening_time_->setEnabled(on);
    });

    m_morning_ = new QCheckBox(QString::fromUtf8("Автоскан при старте"), scheduleGroup);
    m_morning_->setChecked(QSettings().value(SettingsKeys::MorningScan(), false).toBool());
    connect(m_morning_, &QCheckBox::toggled, this, [](bool on) {
        QSettings().setValue(SettingsKeys::MorningScan(), on);
    });

    auto* eveningRow = new QHBoxLayout();
    eveningRow->setSpacing(10);
    eveningRow->addWidget(m_evening_);
    eveningRow->addWidget(m_evening_time_);
    eveningRow->addStretch(1);

    scheduleLayout->addWidget(m_autopilot_);
    scheduleLayout->addLayout(eveningRow);
    scheduleLayout->addWidget(m_morning_);
    layout->addWidget(scheduleGroup);

    auto* capsRow = new QHBoxLayout();
    capsRow->setSpacing(10);
    auto makeCaps = [](const QString& title, const std::vector<CapabilityEntry>& items,
                       const QString& color) {
        auto* edit = new QTextEdit();
        edit->setReadOnly(true);
        edit->setMinimumHeight(150);
        edit->setMaximumHeight(190);
        edit->setStyleSheet(QString(
            "QTextEdit { background-color: %1; color: %2; border: 1px solid %3;"
            " border-radius: 8px; padding: 8px; font-size: 12px; }")
                                .arg(theme::Colors::BG_SURFACE, theme::Colors::TEXT_SECONDARY,
                                     theme::Colors::BORDER));
        QString html = QString("<b><span style=\"color:%1\">%2</span></b><br>").arg(color, title);
        for (const auto& item : items)
            html += QString::fromUtf8("&bull; <b>") + QString::fromStdString(item.title) +
                    QString::fromUtf8("</b> - ") + QString::fromStdString(item.detail) +
                    QString::fromUtf8("<br>");
        edit->setHtml(html);
        return edit;
    };
    const bool elevated = SystemAudit::isAdmin();
    capsRow->addWidget(makeCaps(QString::fromUtf8("Что GNO умеет менять"),
                                CapabilityMatrix::canDo(elevated), theme::Colors::SUCCESS),
                       1);
    capsRow->addWidget(makeCaps(QString::fromUtf8("Чего GNO не делает"),
                                CapabilityMatrix::cannotDo(), theme::Colors::DANGER),
                       1);
    layout->addLayout(capsRow);

    auto* histRow = new QHBoxLayout();
    auto* histTitle = new QLabel(QString::fromUtf8("История изменений"), content);
    histTitle->setStyleSheet(QString("font-size: 15px; font-weight: 600; color: %1; background: transparent;")
                                 .arg(theme::Colors::TEXT_PRIMARY));
    m_rollback_sel_btn_ = new QPushButton(QString::fromUtf8("Откатить выбранную"), content);
    m_rollback_sel_btn_->setEnabled(false);
    m_rollback_sel_btn_->setCursor(Qt::PointingHandCursor);
    histRow->addWidget(histTitle);
    histRow->addStretch();
    histRow->addWidget(m_rollback_sel_btn_);

    m_history_ = new QTableWidget(0, 3, content);
    m_history_->setHorizontalHeaderLabels({QString::fromUtf8("Время"),
                                           QString::fromUtf8("Транзакция"),
                                           QString::fromUtf8("Статус")});
    m_history_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_history_->setColumnWidth(0, 170);
    m_history_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_history_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_history_->setColumnWidth(2, 130);
    m_history_->setMinimumHeight(170);
    m_history_->setMaximumHeight(230);
    m_history_->verticalHeader()->setVisible(false);
    m_history_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_history_->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_history_->setSelectionMode(QAbstractItemView::SingleSelection);
    m_history_->setAlternatingRowColors(true);
    m_history_->setStyleSheet(m_table_->styleSheet());

    layout->addLayout(histRow);
    layout->addWidget(m_history_);
    layout->addWidget(m_result_label_);
    layout->addStretch();

    connect(m_rollback_sel_btn_, &QPushButton::clicked,
            this, &RemediationWidget::onRollbackSelectedClicked);
    // Кнопка «Откатить выбранную» активна только для строк, которые движок
    // действительно может откатить (Applied/частично применённые), а не для
    // любой выделенной строки, включая уже откаченные.
    connect(m_history_, &QTableWidget::itemSelectionChanged, this,
            [this]() { updateSelectedRollbackButton(); });

    connect(m_observe_btn_, &QPushButton::clicked, this, &RemediationWidget::onObserveClicked);
    connect(m_apply_btn_, &QPushButton::clicked, this, &RemediationWidget::onApplyClicked);
    connect(m_rollback_btn_, &QPushButton::clicked, this, &RemediationWidget::onRollbackClicked);

    refreshHistory();
}


void RemediationWidget::onObserveClicked() {
    if (busy_) return;
    setBusy(true);
    m_status_label_->setText(QString::fromUtf8("Проверяю состояние Windows (только чтение)…"));

    std::thread([this]() {
        auto api = std::make_unique<WindowsStateApi>();
        auto store_dir = std::filesystem::path(gno::remediation::defaultBackupsDir());
        auto store = std::make_unique<JsonBackupStore>(store_dir.string());
        RemediationService service(*api, *store, &defaultTargetResolver);

        auto observed = service.observeAll();

        QMetaObject::invokeMethod(this, [this, observed]() {
            setBusy(false);
            if (!observed.ok()) {
                m_status_label_->setText(QString::fromUtf8("Не удалось опросить состояние."));
                return;
            }

            auto views = observed.value();
            m_table_->setRowCount(static_cast<int>(views.size()));
            int recommended = 0;
            int unavailable = 0;
            for (int row = 0; row < static_cast<int>(views.size()); ++row) {
                const auto& view = views[static_cast<std::size_t>(row)];
                if (view.recommended) ++recommended;
                if (view.status == ActionStatus::Unsupported) ++unavailable;
                auto* name_item = new QTableWidgetItem(QString::fromStdString(view.display_name));
                auto* status_item = new QTableWidgetItem(QString::fromStdString(view.status_text));
                auto* state_item = new QTableWidgetItem(QString::fromStdString(view.current_state));
                auto* desc_item = new QTableWidgetItem(QString::fromStdString(view.description));
                status_item->setForeground(QBrush(QColor(statusColor(view))));
                m_table_->setItem(row, 0, name_item);
                m_table_->setItem(row, 1, status_item);
                m_table_->setItem(row, 2, state_item);
                m_table_->setItem(row, 3, desc_item);
            }
            m_table_->resizeRowsToContents();

            QString note = QString::fromUtf8("Рекомендуется к изменению: %1. Недоступно сейчас: %2.")
                               .arg(recommended)
                               .arg(unavailable);
            refreshHistory();
            // Результат применения показываем вместе со свежей сводкой, а не
            // вместо неё: иначе сообщение «применено N / не удалось M» жило
            // меньше секунды и пользователь его не видел.
            if (!m_apply_summary_.isEmpty()) {
                m_status_label_->setText(m_apply_summary_ + " " + note);
                m_apply_summary_.clear();
            } else {
                m_status_label_->setText(note);
            }
            m_result_label_->append(QString::fromUtf8("[%1] Проверка состояния: рекомендовано %2.")
                                        .arg(QTime::currentTime().toString("HH:mm:ss"))
                                        .arg(recommended));
        }, Qt::QueuedConnection);
    }).detach();
}

void RemediationWidget::onApplyClicked() {
    if (busy_) return;
    setBusy(true);
    m_status_label_->setText(QString::fromUtf8("Применяю исправления с резервной копией…"));

    std::thread([this]() {
        auto api = std::make_unique<WindowsStateApi>();
        auto store_dir = std::filesystem::path(gno::remediation::defaultBackupsDir());
        auto store = std::make_unique<JsonBackupStore>(store_dir.string());

        RemediationService service(*api, *store, &defaultTargetResolver);

        auto applied = service.applyAll();

        QMetaObject::invokeMethod(this, [this, applied]() {
            setBusy(false);
            // Частичный успех движок возвращает как ошибку (часть действий
            // упала), но применённый префикс существует и откатывается.
            // Поэтому считаем по фактическим результатам, а не по ok().
            const auto& outcome = applied.value();
            int applied_count = 0;
            int failed_count = 0;
            for (const auto& o : outcome.outcomes) {
                if (o.status == ActionStatus::Applied || o.status == ActionStatus::AlreadyConfigured)
                    ++applied_count;
                else if (o.attempted)
                    ++failed_count;
            }
            const int skipped_count = static_cast<int>(outcome.unsupported_actions.size());

            const QString stamp = QTime::currentTime().toString("HH:mm:ss");
            QString skipped_note;
            if (skipped_count > 0)
                skipped_note = QString::fromUtf8(" Пропущено %1: на этой машине неприменимо.")
                                   .arg(skipped_count);
            QString summary;
            QString log_line;
            if (applied_count > 0 && failed_count == 0) {
                summary = QString::fromUtf8("Готово: применено действий %1.%2 Откат доступен кнопкой "
                                            "«Откатить последнее».")
                              .arg(applied_count)
                              .arg(skipped_note);
                log_line = QString::fromUtf8("[%1] Применено действий: %2.%3")
                               .arg(stamp)
                               .arg(applied_count)
                               .arg(skipped_note);
            } else if (applied_count > 0) {
                summary = QString::fromUtf8("Применено %1, не удалось %2 — подробности в таблице "
                                            "выше.%3 Применённое откатывается одной кнопкой.")
                              .arg(applied_count)
                              .arg(failed_count)
                              .arg(skipped_note);
                log_line = QString::fromUtf8("[%1] Частично: применено %2, ошибок %3.%4")
                               .arg(stamp)
                               .arg(applied_count)
                               .arg(failed_count)
                               .arg(skipped_note);
            } else {
                const QString detail = outcome.detail.empty()
                                           ? QString::fromUtf8("ничего не изменилось")
                                           : QString::fromStdString(outcome.detail);
                summary = QString::fromUtf8("Ничего не применено: %1.").arg(detail);
                log_line = QString::fromUtf8("[%1] Применение не выполнено: %2.").arg(stamp, detail);
            }
            // Запоминаем итог: следующий же «Проверить состояние» перезапишет
            // подпись, и без этого сообщение о результате исчезало мгновенно.
            m_apply_summary_ = summary;
            m_status_label_->setText(summary);
            m_result_label_->append(log_line);
            refreshHistory();
            // Изменения применены здесь — «Прайм-сетап» должен увидеть, что
            // теперь есть что откатывать.
            emit journalChanged();
            m_observe_btn_->click();
        }, Qt::QueuedConnection);
    }).detach();
}

void RemediationWidget::onRollbackClicked() {
    if (busy_) return;
    setBusy(true);

    std::thread([this]() {
        // Через BoostGate, а не через service.rollback() напрямую: только он
        // знает про маркер активного буста и снимет его после отката. Пока
        // откат шёл мимо, маркер оставался, трей продолжал показывать «буст
        // включён», а повторный откат падал на уже откаченной транзакции.
        BoostGate gate(gno::remediation::defaultBackupsDir());
        const auto rolled = gate.revertBoost();

        QMetaObject::invokeMethod(this, [this, rolled]() {
            setBusy(false);
            const QString stamp = QTime::currentTime().toString("HH:mm:ss");
            if (rolled.ok) {
                const QString detail = QString::fromStdString(rolled.detail);
                m_status_label_->setText(QString::fromUtf8("Откат: %1.").arg(detail));
                m_result_label_->append(QString::fromUtf8("[%1] %2.").arg(stamp, detail));
            } else {
                const QString detail = rolled.detail.empty()
                                           ? QString::fromUtf8("откат не завершён")
                                           : QString::fromStdString(rolled.detail);
                m_status_label_->setText(QString::fromUtf8("Откат не удался: %1").arg(detail));
                m_result_label_->append(
                    QString::fromUtf8("[%1] Откат: %2.").arg(stamp, detail));
            }
            refreshHistory();
            emit journalChanged();
        }, Qt::QueuedConnection);
    }).detach();
}

QString RemediationWidget::backupDir()
{
    return QString::fromStdString(gno::remediation::defaultBackupsDir());
}

void RemediationWidget::setBusy(bool busy) {
    busy_ = busy;
    if (busy) QApplication::setOverrideCursor(Qt::WaitCursor);
    else QApplication::restoreOverrideCursor();
    m_observe_btn_->setEnabled(!busy);
    m_apply_btn_->setEnabled(!busy);
    if (busy) {
        // Пока операция идёт, откат запускать нельзя: две транзакции писали бы
        // в один журнал одновременно.
        m_rollback_btn_->setEnabled(false);
        m_rollback_sel_btn_->setEnabled(false);
    } else {
        updateRollbackButtons();
    }
}

void RemediationWidget::updateRollbackButtons() {
    const bool has_revertable = !m_revertable_ids_.isEmpty();
    if (m_rollback_btn_) {
        m_rollback_btn_->setEnabled(!busy_ && has_revertable);
        m_rollback_btn_->setToolTip(
            has_revertable
                ? QString::fromUtf8("Вернуть параметры к значениям до последнего применения.")
                : QString::fromUtf8("Откатывать нечего: применённых транзакций нет."));
    }
    updateSelectedRollbackButton();
}

void RemediationWidget::updateSelectedRollbackButton() {
    if (!m_rollback_sel_btn_) return;
    const int row = m_history_->currentRow();
    const QTableWidgetItem* id_item = row >= 0 ? m_history_->item(row, 1) : nullptr;
    const bool revertable =
        !busy_ && id_item && m_revertable_ids_.contains(id_item->text());
    m_rollback_sel_btn_->setEnabled(revertable);
    if (revertable)
        m_rollback_sel_btn_->setToolTip(
            QString::fromUtf8("Откатить именно эту транзакцию и вернуть прежние значения."));
    else if (!id_item)
        m_rollback_sel_btn_->setToolTip(
            QString::fromUtf8("Сначала выберите транзакцию в списке."));
    else
        m_rollback_sel_btn_->setToolTip(
            QString::fromUtf8("Эту транзакцию откатить нельзя: изменения уже откачены "
                              "или не применялись."));
}

void RemediationWidget::refreshHistory()
{
    JsonBackupStore store(backupDir().toStdString());
    auto history = store.list();
    if (!history.ok()) return;

    const int rows = static_cast<int>(history.value().size());
    m_history_->setRowCount(rows);
    m_revertable_ids_.clear();

    for (int row = 0; row < rows; ++row) {
        const auto& summary = history.value()[static_cast<std::size_t>(row)];
        // Загружаем запись, чтобы спросить у движка то же правило отката и
        // показать честный статус. Пропускаем только заведомо неоткатываемые
        // статусы — их разбор ничего не изменит.
        std::optional<TransactionRecord> record;
        switch (summary.status) {
            case TransactionStatus::Applied:
            case TransactionStatus::Failed:
            case TransactionStatus::Cancelled:
            case TransactionStatus::RollbackFailed: {
                auto loaded = store.load(summary.transaction_id);
                if (loaded.ok()) record = loaded.value();
                break;
            }
            default:
                break;
        }
        if (record && isRollbackEligible(*record))
            m_revertable_ids_.insert(QString::fromStdString(summary.transaction_id));

        m_history_->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(summary.created_at)));
        m_history_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(summary.transaction_id)));
        m_history_->setItem(
            row, 2,
            new QTableWidgetItem(
                transactionStatusText(summary, record ? &*record : nullptr)));
    }

    updateRollbackButtons();
}

void RemediationWidget::onRollbackSelectedClicked()
{
    const int row = m_history_->currentRow();
    if (row < 0) return;
    const auto* id_item = m_history_->item(row, 1);
    if (!id_item) return;
    const QString transaction_id = id_item->text();
    // Кнопка включается только для откатываемых строк, но подстраховываемся:
    // движок всё равно отказал бы, а пользователь получил бы непонятную ошибку.
    if (!m_revertable_ids_.contains(transaction_id)) {
        m_result_label_->append(
            QString::fromUtf8("[%1] Эту транзакцию откатить нельзя: изменения уже откачены "
                              "или не применялись.")
                .arg(QTime::currentTime().toString("HH:mm:ss")));
        return;
    }
    if (busy_) return;
    setBusy(true);

    std::thread([this, transaction_id]() {
        BoostGate gate(gno::remediation::defaultBackupsDir());
        const auto rolled = gate.revertTransaction(transaction_id.toStdString());

        QMetaObject::invokeMethod(this, [this, rolled]() {
            setBusy(false);
            const QString stamp = QTime::currentTime().toString("HH:mm:ss");
            if (rolled.ok) {
                m_result_label_->append(QString::fromUtf8("[%1] Откат выбранной транзакции выполнен.")
                                            .arg(stamp));
            } else {
                const QString detail = rolled.detail.empty()
                                           ? QString::fromUtf8("откат не завершён")
                                           : QString::fromStdString(rolled.detail);
                m_result_label_->append(QString::fromUtf8("[%1] Ошибка отката: %2")
                                            .arg(stamp, detail));
            }
            refreshHistory();
            emit journalChanged();
        }, Qt::QueuedConnection);
    }).detach();
}
} // namespace gno
