#include "ui/remediation_widget.h"

#include "core/capability_matrix.h"
#include "core/system_audit.h"
#include "remediation/target_discovery.h"
#include "core/game_profiles.h"
#include "remediation/remediation_service.h"
#include "ui/theme.h"

#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QFrame>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QTime>
#include <QVBoxLayout>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <thread>

#ifdef PLATFORM_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace gno {

using namespace remediation;

namespace {

QString statusColor(const ActionView& view) {
    if (view.recommended) return theme::Colors::WARNING;
    if (view.status == ActionStatus::AlreadyConfigured) return theme::Colors::SUCCESS;
    return theme::Colors::TEXT_SECONDARY;
}

std::optional<ActionTarget> primaryInterfaceTarget() {
    return discoverPrimaryInterface();
}

struct RunningGameRef {
    std::uint32_t pid = 0;
    std::uint64_t creation_time = 0;
    std::string path;
};

std::optional<RunningGameRef> findRunningGameProcess() {
    if (auto game = discoverRunningGameProcess())
        return RunningGameRef{game->pid, game->creation_time, game->path};
    return std::nullopt;
}

} // namespace

RemediationWidget::RemediationWidget(QWidget* parent) : QWidget(parent) {
    setupUI();
}

void RemediationWidget::setupUI() {
    setObjectName("remediationPage");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(14);

    auto* title = new QLabel(QString::fromUtf8("Оптимизация Windows и сети"), this);
    title->setStyleSheet(QString("font-size: 20px; font-weight: 600; color: %1;")
                             .arg(theme::Colors::TEXT_PRIMARY));

    auto* subtitle = new QLabel(
        QString::fromUtf8("Только действия из белого списка. Перед изменением создаётся резервная копия, "
                          "результат проверяется. Откат доступен одной кнопкой."),
        this);
    subtitle->setStyleSheet(QString("color: %1;").arg(theme::Colors::TEXT_SECONDARY));
    subtitle->setWordWrap(true);

    auto* buttons = new QHBoxLayout();
    m_observe_btn_ = new QPushButton(QString::fromUtf8("Проверить состояние"), this);
    m_apply_btn_ = new QPushButton(QString::fromUtf8("Применить рекомендации"), this);
    m_rollback_btn_ = new QPushButton(QString::fromUtf8("Откатить последнее"), this);
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
        QString::fromUtf8("Нажмите «Проверить состояние», чтобы увидеть, что можно улучшить."), this);
    m_status_label_->setStyleSheet(QString("color: %1;").arg(theme::Colors::TEXT_SECONDARY));
    m_status_label_->setWordWrap(true);

    m_table_ = new QTableWidget(0, 4, this);
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
    m_table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table_->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table_->setAlternatingRowColors(true);
    m_table_->setStyleSheet(QString(
        "QTableWidget { background-color: %1; color: %2; gridline-color: rgba(255,255,255,0.06);"
        " border: 1px solid rgba(255,255,255,0.08); border-radius: 8px; }"
        "QHeaderView::section { background-color: %3; color: %4; padding: 8px; border: none;"
        " font-weight: 600; }")
                                .arg(theme::Colors::BG_SURFACE, theme::Colors::TEXT_PRIMARY,
                                     theme::Colors::BG_ELEVATED, theme::Colors::TEXT_SECONDARY));

    m_result_label_ = new QTextEdit(this);
    m_result_label_->setReadOnly(true);
    m_result_label_->setMaximumHeight(110);
    m_result_label_->setPlaceholderText(QString::fromUtf8("Журнал операций…"));
    m_result_label_->setStyleSheet(QString(
        "QTextEdit { background-color: %1; color: %2; border: 1px solid rgba(255,255,255,0.08);"
        " border-radius: 8px; padding: 8px; }")
                                       .arg(theme::Colors::BG_SURFACE, theme::Colors::TEXT_SECONDARY));

    layout->addWidget(title);
    layout->addWidget(subtitle);
    layout->addLayout(buttons);
    layout->addWidget(m_status_label_);
    layout->addWidget(m_table_, 1);

    auto* capsRow = new QHBoxLayout();
    capsRow->setSpacing(10);
    auto makeCaps = [](const QString& title, const std::vector<CapabilityEntry>& items,
                       const QString& color) {
        auto* edit = new QTextEdit();
        edit->setReadOnly(true);
        edit->setMaximumHeight(150);
        edit->setStyleSheet(QString(
            "QTextEdit { background-color: %1; color: %2; border: 1px solid rgba(255,255,255,0.08);"
            " border-radius: 8px; padding: 8px; font-size: 12px; }")
                                .arg(theme::Colors::BG_SURFACE, theme::Colors::TEXT_SECONDARY));
        QString html = QString("<b><span style=\"color:%1\">%2</span></b><br>").arg(color, title);
        for (const auto& item : items)
            html += QString::fromUtf8("&bull; <b>") + QString::fromStdString(item.title) +
                    QString::fromUtf8("</b> - ") + QString::fromStdString(item.detail) +
                    QString::fromUtf8("<br>");
        edit->setHtml(html);
        return edit;
    };
    const bool elevated = SystemAudit::isAdmin();
    capsRow->addWidget(makeCaps(
        QString::fromUtf8("\xD0\xA7\xD1\x82\xD0\xBE\x20\x47\x4E\x4F\x20\xD1\x83\xD0\xBC\xD0\xB5\xD0\xB5\xD1\x82\x20\xD0\xBC\xD0\xB5\xD0\xBD\xD1\x8F\xD1\x82\xD1\x8C"),
        CapabilityMatrix::canDo(elevated), theme::Colors::SUCCESS), 1);
    capsRow->addWidget(makeCaps(
        QString::fromUtf8("\xD0\xA7\xD0\xB5\xD0\xB3\xD0\xBE\x20\x47\x4E\x4F\x20\xD0\xBD\xD0\xB5\x20\xD0\xB4\xD0\xB5\xD0\xBB\xD0\xB0\xD0\xB5\xD1\x82"),
        CapabilityMatrix::cannotDo(), QColor(0xEF,0x44,0x44).name()), 1);
    layout->addLayout(capsRow);

    layout->addWidget(m_result_label_);

    connect(m_observe_btn_, &QPushButton::clicked, this, &RemediationWidget::onObserveClicked);
    connect(m_apply_btn_, &QPushButton::clicked, this, &RemediationWidget::onApplyClicked);
    connect(m_rollback_btn_, &QPushButton::clicked, this, &RemediationWidget::onRollbackClicked);
}


void RemediationWidget::onObserveClicked() {
    m_observe_btn_->setEnabled(false);
    m_status_label_->setText(QString::fromUtf8("Проверяю состояние Windows (только чтение)…"));
    QApplication::setOverrideCursor(Qt::WaitCursor);

    std::thread([this]() {
        auto api = std::make_unique<WindowsStateApi>();
        auto store_dir = std::filesystem::path(std::getenv("APPDATA") ? std::getenv("APPDATA") : ".") /
                         "GNO" / "Backups";
        auto store = std::make_unique<JsonBackupStore>(store_dir.string());
        RemediationService service(*api, *store, [](const std::vector<FixAction*>& actions) {
            std::vector<std::optional<ActionTarget>> targets;
            targets.reserve(actions.size());
            const auto iface = primaryInterfaceTarget();
            const auto game = findRunningGameProcess();
            for (auto* action : actions) {
                switch (action->id()) {
                    case ActionId::Dns:
                    case ActionId::Mtu:
                        targets.push_back(iface);
                        break;
                    case ActionId::FullscreenOptimizations:
                        if (game) targets.push_back(ActionTarget{ExecutableTarget{game->path}});
                        else targets.push_back(std::nullopt);
                        break;
                    case ActionId::ProcessPriority:
                        if (game)
                            targets.push_back(ActionTarget{
                                ProcessTarget{game->pid, game->creation_time, game->path}});
                        else targets.push_back(std::nullopt);
                        break;
                    default:
                        targets.push_back(ActionTarget{NoTarget{}});
                }
            }
            return targets;
        });

        auto observed = service.observeAll();

        QMetaObject::invokeMethod(this, [this, observed]() {
            QApplication::restoreOverrideCursor();
            m_observe_btn_->setEnabled(true);
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
            m_status_label_->setText(note);
            m_result_label_->append(QString::fromUtf8("[%1] Проверка состояния: рекомендовано %2.")
                                        .arg(QTime::currentTime().toString("HH:mm:ss"))
                                        .arg(recommended));
        }, Qt::QueuedConnection);
    }).detach();
}

void RemediationWidget::onApplyClicked() {
    m_apply_btn_->setEnabled(false);
    m_status_label_->setText(QString::fromUtf8("Применяю исправления с резервной копией…"));
    QApplication::setOverrideCursor(Qt::WaitCursor);

    std::thread([this]() {
        auto api = std::make_unique<WindowsStateApi>();
        auto store_dir = std::filesystem::path(std::getenv("APPDATA") ? std::getenv("APPDATA") : ".") /
                         "GNO" / "Backups";
        auto store = std::make_unique<JsonBackupStore>(store_dir.string());

        RemediationService service(
            *api, *store, [](const std::vector<FixAction*>& actions) {
                std::vector<std::optional<ActionTarget>> targets;
                targets.reserve(actions.size());
                const auto iface = primaryInterfaceTarget();
                const auto game = findRunningGameProcess();
                for (auto* action : actions) {
                    switch (action->id()) {
                        case ActionId::Dns:
                        case ActionId::Mtu:
                            targets.push_back(iface);
                            break;
                        case ActionId::FullscreenOptimizations:
                            if (game) targets.push_back(ActionTarget{ExecutableTarget{game->path}});
                            else targets.push_back(std::nullopt);
                            break;
                        case ActionId::ProcessPriority:
                            if (game)
                                targets.push_back(ActionTarget{
                                    ProcessTarget{game->pid, game->creation_time, game->path}});
                            else targets.push_back(std::nullopt);
                            break;
                        default:
                            targets.push_back(ActionTarget{NoTarget{}});
                    }
                }
                return targets;
            });

        auto applied = service.applyAll();

        QMetaObject::invokeMethod(this, [this, applied]() {
            QApplication::restoreOverrideCursor();
            m_apply_btn_->setEnabled(true);
            if (applied.ok() && applied.value().succeeded) {
                m_status_label_->setText(QString::fromUtf8("Готово: применено действий %1. Откат доступен кнопкой «Откатить последнее».")
                                             .arg(static_cast<int>(applied.value().outcomes.size())));
                m_result_label_->append(QString::fromUtf8("[%1] Применено успешно.")
                                            .arg(QTime::currentTime().toString("HH:mm:ss")));
            } else {
                const QString detail = applied.ok() ? QString::fromStdString(applied.value().detail)
                                                    : QString::fromStdString(applied.error().detail);
                m_status_label_->setText(QString::fromUtf8("Прервано: %1.").arg(detail));
                m_result_label_->append(QString::fromUtf8("[%1] Применение прервано: %2.")
                                            .arg(QTime::currentTime().toString("HH:mm:ss"), detail));
            }
            m_observe_btn_->click();
        }, Qt::QueuedConnection);
    }).detach();
}

void RemediationWidget::onRollbackClicked() {
    m_rollback_btn_->setEnabled(false);
    QApplication::setOverrideCursor(Qt::WaitCursor);

    std::thread([this]() {
        auto api = std::make_unique<WindowsStateApi>();
        auto store_dir = std::filesystem::path(std::getenv("APPDATA") ? std::getenv("APPDATA") : ".") /
                         "GNO" / "Backups";
        auto store = std::make_unique<JsonBackupStore>(store_dir.string());
        RemediationService service(*api, *store);

        auto history = service.history();
        Result<ApplyOutcome> rolled = Fail(RemediationError::InternalFailure, "нет сохранённых транзакций");
        if (history.ok() && !history.value().empty())
            rolled = service.rollback(history.value().front().transaction_id);

        QMetaObject::invokeMethod(this, [this, rolled]() {
            QApplication::restoreOverrideCursor();
            m_rollback_btn_->setEnabled(true);
            if (rolled.ok() && rolled.value().succeeded) {
                m_status_label_->setText(QString::fromUtf8("Откат выполнен: %1")
                                             .arg(QString::fromStdString(rolled.value().detail)));
                m_result_label_->append(QString::fromUtf8("[%1] Изменения откатены к исходным.")
                                            .arg(QTime::currentTime().toString("HH:mm:ss")));
            } else {
                const QString detail = rolled.ok() ? QString::fromStdString(rolled.value().detail)
                                                   : QString::fromStdString(rolled.error().detail);
                m_status_label_->setText(QString::fromUtf8("Откат не удался: %1").arg(detail));
                m_result_label_->append(QString::fromUtf8("[%1] Откат: %2.")
                                            .arg(QTime::currentTime().toString("HH:mm:ss"), detail));
            }
        }, Qt::QueuedConnection);
    }).detach();
}

} // namespace gno
