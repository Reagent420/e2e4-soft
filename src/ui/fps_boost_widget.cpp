#include "fps_boost_widget.h"
#include "theme.h"
#include "core/fps_boost.h"
#include "core/system_audit.h"
#include "core/system_manager.h"

#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <algorithm>

namespace gno {

using namespace fpsboost;

namespace {
QString serviceStatusText(const ServiceInfo& info) {
    if (info.start_type == 4) return QStringLiteral("DISABLED");
    return info.running ? QStringLiteral("RUNNING") : QStringLiteral("STOPPED");
}
QColor serviceColor(const ServiceInfo& info) {
    if (info.start_type == 4) return QColor(theme::Colors::TEXT_TERTIARY);
    return info.running ? QColor(theme::Colors::SUCCESS) : QColor(theme::Colors::WARNING);
}
} // namespace

FpsBoostWidget::FpsBoostWidget(QWidget* parent) : QWidget(parent) { setupUI(); }

void FpsBoostWidget::setupUI() {
    setObjectName("fpsBoostPage");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 20, 24, 20);
    root->setSpacing(14);

    // ---- Timer Resolution ----
    auto* timerTitle = new QLabel(QStringLiteral("TIMER RESOLUTION"), this);
    timerTitle->setStyleSheet(QString(
        "font-size:13px; font-weight:700; letter-spacing:2px;"
        "color:%1; background:transparent;").arg(theme::Colors::ACCENT_NEON));

    auto* timerRow = new QHBoxLayout();
    m_timer_btn_ = new QPushButton("Enable 0.5ms", this);
    m_timer_btn_->setObjectName("boostButton");
    m_timer_btn_->setFixedWidth(200);
    m_timer_label_ = new QLabel(this);
    timerRow->addWidget(m_timer_btn_);
    timerRow->addWidget(m_timer_label_, 1);
    connect(m_timer_btn_, &QPushButton::clicked,
            this, &FpsBoostWidget::onTimerToggle);

    // ---- RAM Cleaner ----
    auto* ramTitle = new QLabel(QStringLiteral("RAM CLEANER"), this);
    ramTitle->setStyleSheet(timerTitle->styleSheet());
    auto* ramRow = new QHBoxLayout();
    m_ram_btn_ = new QPushButton("Clean RAM", this);
    m_ram_btn_->setFixedWidth(200);
    m_ram_label_ = new QLabel(QStringLiteral(" "), this);
    ramRow->addWidget(m_ram_btn_);
    ramRow->addWidget(m_ram_label_, 1);
    connect(m_ram_btn_, &QPushButton::clicked,
            this, &FpsBoostWidget::onRamClean);

    // ---- Services ----
    auto* svcTitle = new QLabel(QStringLiteral("GAMING SERVICES"), this);
    svcTitle->setStyleSheet(timerTitle->styleSheet());
    m_services_table_ = new QTableWidget(3, 3, this);
    m_services_table_->setHorizontalHeaderLabels({
        QStringLiteral("SERVICE"), QStringLiteral("STATUS"),
        QStringLiteral("ACTION")});
    m_services_table_->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::Stretch);
    m_services_table_->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::Fixed);
    m_services_table_->horizontalHeader()->setSectionResizeMode(
        2, QHeaderView::Fixed);
    m_services_table_->verticalHeader()->setVisible(false);
    m_services_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_services_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_services_table_->setAlternatingRowColors(true);
    m_services_table_->setMaximumHeight(140);

    // ---- Startup Programs ----
    auto* startupTitle = new QLabel(QStringLiteral("STARTUP PROGRAMS"), this);
    startupTitle->setStyleSheet(timerTitle->styleSheet());
    m_startup_table_ = new QTableWidget(0, 3, this);
    m_startup_table_->setHorizontalHeaderLabels({
        QStringLiteral("NAME"), QStringLiteral("COMMAND"),
        QStringLiteral("ENABLED")});
    m_startup_table_->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::Fixed);
    m_startup_table_->setColumnWidth(0, 180);
    m_startup_table_->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::Stretch);
    m_startup_table_->horizontalHeader()->setSectionResizeMode(
        2, QHeaderView::Fixed);
    m_startup_table_->setColumnWidth(2, 90);
    m_startup_table_->verticalHeader()->setVisible(false);
    m_startup_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_startup_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_startup_table_->setAlternatingRowColors(true);
    m_startup_table_->setMaximumHeight(180);

    connect(m_services_table_, &QTableWidget::cellClicked,
            this, &FpsBoostWidget::onServiceAction);
    connect(m_startup_table_, &QTableWidget::cellClicked,
            this, &FpsBoostWidget::onStartupToggle);

    // ---- Layout assembly ----
    root->addWidget(timerTitle);
    root->addLayout(timerRow);
    root->addWidget(ramTitle);
    root->addLayout(ramRow);
    root->addWidget(svcTitle);
    root->addWidget(m_services_table_);

    root->addWidget(startupTitle);
    root->addWidget(m_startup_table_);

    onRefreshServices();
    refreshStartup();

    // Инициализация подписи таймера: без этого она пуста до первого клика.
    double res = currentTimerResolution() / 10000.0;
    m_timer_label_->setText(
        QString("Timer: %1 ms").arg(res, 0, 'f', 2));
}

void FpsBoostWidget::onTimerToggle() {
    if (!timer_active_) {
        if (setTimerResolution(5000)) {
            timer_active_ = true;
            m_timer_btn_->setText("Disable 0.5ms");
        }
    } else {
        releaseTimerResolution();
        timer_active_ = false;
        m_timer_btn_->setText("Enable 0.5ms");
    }
    double res = currentTimerResolution() / 10000.0;
    m_timer_label_->setText(
        QString("Timer: %1 ms").arg(res, 0, 'f', 2));
}

void FpsBoostWidget::onRamClean() {
    m_ram_btn_->setEnabled(false);
    QApplication::processEvents();
    auto stats = cleanRam();
    double freed_mb = stats.bytes_freed_estimate / (1024.0 * 1024);
    m_ram_label_->setText(
        QString("Freed: %1 MB (%2 processes)")
            .arg(static_cast<int>(freed_mb)).arg(stats.processes_trimmed));
    m_ram_btn_->setEnabled(true);
}

void FpsBoostWidget::onServiceAction(int row, int col) {
    if (col != 2) return;
    if (row < 0 || row >= 3) return;
    struct SvcDef { const char* name; const char* display; };
    static const SvcDef services[] = {
        {"SysMain", "SysMain (Superfetch)"},
        {"WSearch", "Windows Search"},
        {"DoSvc", "Delivery Optimization"},
    };
    auto info = queryService(services[row].name);
    // Toggle: disabled -> auto, anything else -> disable
    ServiceAction action = (info.start_type == 4) ? ServiceAction::AutoStart
                                                  : ServiceAction::Disable;
    serviceControl(services[row].name, action);
    onRefreshServices();
}

void FpsBoostWidget::onStartupToggle(int row, int col) {
    if (col != 2) return;
    // Определяем программу по данным из самой таблицы, а не по индексу строки:
    // enumStartupPrograms() может вернуть другой порядок или другой набор
    // записей к моменту клика (например, добавилась новая запись в автозапуск) —
    // тогда индекс укажет на чужую программу, и мы переключим не то, что видел
    // пользователь. Сверяем и имя, и команду: записи с одинаковым именем могут
    // лежать в разных ветках (HKCU Run / HKLM Run).
    QTableWidgetItem* nameItem = m_startup_table_->item(row, 0);
    QTableWidgetItem* cmdItem = m_startup_table_->item(row, 1);
    if (!nameItem) return;
    const QString target = nameItem->text();
    const QString targetCmd = cmdItem ? cmdItem->text() : QString();

    auto entries = enumStartupPrograms();
    auto it = std::find_if(entries.begin(), entries.end(),
        [&](const StartupEntry& e) {
            return QString::fromStdString(e.name) == target &&
                   QString::fromStdString(e.command) == targetCmd;
        });
    if (it == entries.end()) return;
    setStartupEnabled(it->location, it->name, !it->enabled);
    refreshStartup();
}

void FpsBoostWidget::onRefreshServices() {
    struct SvcDef { const char* name; const char* display; };
    static const SvcDef services[] = {
        {"SysMain", "SysMain (Superfetch)"},
        {"WSearch", "Windows Search"},
        {"DoSvc", "Delivery Optimization"},
    };

    bool elevated = SystemAudit::isAdmin();
    m_services_table_->clearContents();
    m_services_table_->setRowCount(3);
    for (int i = 0; i < 3; ++i) {
        auto info = queryService(services[i].name);

        auto* name_item = new QTableWidgetItem(
            QString::fromUtf8(services[i].display));
        m_services_table_->setItem(i, 0, name_item);

        auto* status_item = new QTableWidgetItem(serviceStatusText(info));
        status_item->setForeground(QBrush(serviceColor(info)));
        m_services_table_->setItem(i, 1, status_item);

        QString action_text = info.start_type == 4
            ? QStringLiteral("Enable") : QStringLiteral("Disable");
        m_services_table_->setItem(i, 2,
            new QTableWidgetItem(action_text));
    }
}

void FpsBoostWidget::refreshStartup() {
    auto entries = enumStartupPrograms();
    m_startup_table_->clearContents();
    m_startup_table_->setRowCount(static_cast<int>(entries.size()));
    for (int r = 0; r < static_cast<int>(entries.size()); ++r) {
        const auto& e = entries[static_cast<std::size_t>(r)];
        m_startup_table_->setItem(r, 0,
            new QTableWidgetItem(QString::fromStdString(e.name)));
        m_startup_table_->setItem(r, 1,
            new QTableWidgetItem(QString::fromStdString(e.command)));
        m_startup_table_->setItem(r, 2,
            new QTableWidgetItem(e.enabled ? "YES" : "NO"));
    }
}

} // namespace gno
