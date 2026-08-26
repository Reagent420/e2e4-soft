#include "fps_boost_widget.h"
#include "theme.h"
#include "core/fps_boost.h"
#include "core/system_audit.h"
#include "core/system_manager.h"

#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QHeaderView>
#include <QTime>

namespace gno {

using namespace fpsboost;

namespace {
QString serviceStatusText(const ServiceInfo& info) {
    if (info.start_type == 4) return "DISABLED";
    return info.running ? "RUNNING" : "STOPPED";
}
QColor serviceColor(const ServiceInfo& info) {
    if (info.start_type == 4) return QColor(theme::Colors::TEXT_TERTIARY);
    return info.running ? QColor(theme::Colors::SUCCESS) : QColor(theme::Colors::WARNING);
}
} // namespace

FpsBoostWidget::FpsBoostWidget(QWidget* parent) : QWidget(parent) { setupUI(); }

void FpsBoostWidget::setupUI() {
    setObjectName("fpsBoostPage");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(14);

    // ---- Timer Resolution ------------------------------------------------
    auto* timer_title = new QLabel(QStringLiteral("TIMER RESOLUTION"), this);
    timer_title->setStyleSheet(QString("font-size:15px; font-weight:700; letter-spacing:2px;"
                                       "color:%1; background:transparent;")
                                   .arg(theme::Colors::ACCENT_NEON));
    layout->addWidget(timer_title);

    auto* timer_row = new QHBoxLayout();
    m_timer_btn_ = new QPushButton(QString::fromUtf8(
        "\xD0\x92\xD0\xBA\xD0\xBB\xD1%8E\xD1%87\xD0%B8\xD1%82\xD1%8C\x200.5ms"), this);
    m_timer_btn_->setObjectName("boostButton");
    m_timer_btn_->setFixedWidth(220);
    m_timer_label_ = new QLabel(
        QString::fromUtf8("\xD0%A2%D0%B0%D0%B9%D0%BC%D0%B5%D1%80 Windows: %1 ms")
            .arg(currentTimerResolution() / 10000.0, 0, 'f', 2), this);
    m_timer_label_->setStyleSheet(QString("color:%1; background:transparent;")
                                      .arg(theme::Colors::TEXT_SECONDARY));
    timer_row->addWidget(m_timer_btn_);
    timer_row->addWidget(m_timer_label_, 1);
    layout->addLayout(timer_row);

    connect(m_timer_btn_, &QPushButton::clicked, this, &FpsBoostWidget::onTimerToggle);

    // ---- RAM Cleaner -----------------------------------------------------
    auto* ram_title = new QLabel(QStringLiteral("RAM CLEANER"), this);
    ram_title->setStyleSheet(timer_title->styleSheet());
    layout->addWidget(ram_title);

    auto* ram_row = new QHBoxLayout();
    m_ram_btn_ = new QPushButton(QString::fromUtf8(
        "\xD0%9E\xD1%87%D0%B8%D1%81%D1%82%D0%B8%D1%82\xD1%8C\x20RAM"), this);
    m_ram_btn_->setFixedWidth(220);
    m_ram_label_ = new QLabel(QStringLiteral(" "), this);
    m_ram_label_->setStyleSheet(QString("color:%1; background:transparent;")
                                    .arg(theme::Colors::TEXT_SECONDARY));
    ram_row->addWidget(m_ram_btn_);
    ram_row->addWidget(m_ram_label_, 1);
    layout->addLayout(ram_row);

    connect(m_ram_btn_, &QPushButton::clicked, this, &FpsBoostWidget::onRamClean);

    // ---- Services ----------------------------------------------------------
    auto* svc_title = new QLabel(QStringLiteral("GAMING SERVICES"), this);
    svc_title->setStyleSheet(timer_title->styleSheet());
    layout->addWidget(svc_title);

    m_services_table_ = new QTableWidget(3, 3, this);
    m_services_table_->setHorizontalHeaderLabels({QStringLiteral("SERVICE"),
                                                  QStringLiteral("STATUS"),
                                                  QString::fromUtf8("\xD0\x94\xD0\xB5%D0%B9%D1%81%D1%82%D0%B2%D0%B8%D0%B5")});
    m_services_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table_style_helper(m_services_table_);
    m_services_table_->setMaximumHeight(140);
    layout->addWidget(m_services_table_);

    auto* refresh_svc = new QPushButton(QStringLiteral("REFRESH"), this);
    refresh_svc->setMaximumWidth(120);
    connect(refresh_svc, &QPushButton::clicked, this, &FpsBoostWidget::onRefreshServices);
    layout->addWidget(refresh_svc);

    // ---- Startup Programs --------------------------------------------------
    auto* startup_title = new QLabel(QStringLiteral("STARTUP PROGRAMS"), this);
    startup_title->setStyleSheet(timer_title->styleSheet());
    layout->addWidget(startup_title);

    m_startup_table_ = new QTableWidget(0, 3, this);
    m_startup_table_->setHorizontalHeaderLabels({QStringLiteral("NAME"),
                                                 QString::fromUtf8("\xD0\x9A\xD0\xBE%D0\xBC%D0%B0%D0\xBD%D0%B4%D0%B0"),
                                                 QStringLiteral("ENABLED")});
    m_startup_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_table_->setColumnWidth(0, 180);
    m_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_table_->setColumnWidth(2, 90);
    m_startup_table_->verticalHeader()->setVisible(false);
    m_startup_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_startup_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_startup_table_->setAlternatingRowColors(true);
    m_startup_table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_startup_table_->setMaximumHeight(200);
    layout->addWidget(m_startup_table_);

    connect(m_startup_table_, &QTableWidget::cellClicked, this, &FpsBoostWidget::onStartupToggle);

    onRefreshServices();

    // ---- Process Manager (v3.1) ----
    auto* procTitle = new QLabel(QStringLiteral("PROCESS MANAGER"), this);
    procTitle->setStyleSheet(QString(
        "font-size:13px; font-weight:700; letter-spacing:2px;"
        "color:%1; background:transparent;").arg(theme::Colors::ACCENT_NEON));

    m_proc_table_ = new QTableWidget(0, 4, this);
    m_proc_table_->setHorizontalHeaderLabels({
        QStringLiteral("PID"),
        QString::fromUtf8("\xD0\x98\xD0\xBC\xD1%8F"),
        QStringLiteral("RAM (MB)"),
        QString::fromUtf8("\xD0\x94%D0%B5%D0%B9%D1\x81%D1%82%D0%B2%D0%B8%D0%B5")});
    m_proc_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_proc_table_->setColumnWidth(0, 70);
    m_proc_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_table_->setColumnWidth(2, 90);
    m_table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_table_->setColumnWidth(3, 130);
    m_table_->verticalHeader()->setVisible(false);
    m_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table_->setAlternatingRowColors(true);

    auto* procBtn = new QPushButton(QString::fromUtf8(
        "\xD0\x9E\xD0%B1%D0%BD%D0%BE%D0%B2%D0%B8%D1%82\xD1%8C"), this);
    connect(procBtn, &QPushButton::clicked, this, [this]() { refreshProcesses(); });

    layout->addWidget(procTitle);
    layout->addWidget(procBtn);
    layout->addWidget(m_proc_table_);

    // ---- GPU Preference ----
    auto* gpuRow = new QHBoxLayout();
    auto* gpuLbl = new QLabel(QString::fromUtf8(
        "GPU Preference"), this);
    gpuLbl->setStyleSheet(QString("color:%1; background:transparent;")
        .arg(theme::Colors::TEXT_SECONDARY));
    m_gpu_combo_ = new QComboBox(this);
    m_gpu_combo_->addItem(QString::fromUtf8("\xD0\x90%D0%B2%D1%82%D0%BE"), 0);
    m_gpu_combo_->addItem(QString::fromUtf8("\xD0%AD%D0%BA%D0%BE%D0%BD"), 1);
    m_gpu_combo_->addItem(QString::fromUtf8("\xD0%92%D1%8B%D1%81%D0%BE%D0%BA%D0%B8%D0%B5"), 2);
    connect(m_gpu_apply_btn_ = new QPushButton(QString::fromUtf8(
        "\xD0\x9F%D1%80%D0%B8%D0%BC%D0%B5%D0%BD%D0%B8%D1%82\xD1%8C"), this),
        &QPushButton::clicked, this, [this]() {
            // GPU preference is set per-exe in the game profiles page
        });
    gpuRow->addWidget(gpuLbl);
    gpuRow->addWidget(m_gpu_combo_);
    gpuRow->addWidget(m_gpu_apply_btn_);
    gpuRow->addStretch();
    layout->addLayout(gpuRow);

    // ---- NIC Driver Info ----
    m_nic_label_ = new QLabel(this);
    m_nic_label_->setStyleSheet(QString(
        "color:%1; font-size:11px; font-family:Consolas;"
        " background:transparent;").arg(theme::Colors::TEXT_TERTIARY));
    layout->addWidget(m_nic_label_);

    layout->addStretch();
}

void FpsBoostWidget::refreshProcesses() {
    auto procs = sysmgr::enumUserProcesses();
    // Filter: only non-system with RAM > 5 MB
    std::vector<sysmgr::ProcInfo> user;
    for (auto& p : procs)
        if (!p.is_system && p.working_set > 5 * 1024 * 1024)
            user.push_back(p);
    std::sort(user.begin(), user.end(),
              [](const auto& a, const auto& b) { return a.working_set > b.working_set; });
    if (user.size() > 30) user.resize(30);

    m_proc_table_->setRowCount(static_cast<int>(user.size()));
    for (int r = 0; r < static_cast<int>(user.size()); ++r) {
        const auto& p = user[static_cast<std::size_t>(r)];
        m_proc_table_->setItem(r, 0,
            new QTableWidgetItem(QString::number(p.pid)));
        m_proc_table_->setItem(r, 1,
            new QTableWidgetItem(QString::fromStdString(p.name)));
        m_proc_table_->setItem(r, 2, new QTableWidgetItem(
            QString::number(static_cast<int>(p.working_set / (1024 * 1024))) + " MB"));
        m_proc_table_->setItem(r, 3, new QTableWidgetItem(
            p.is_system ? "SYS" : "USER"));
    }
    m_proc_table_->resizeRowsToContents();
}

void FpsBoostWidget::onTimerToggle() {
    static bool active = false;
    if (!active) {
        if (setTimerResolution(5000)) {
            active = true;
            m_timer_btn_->setText(QStringLiteral("DISABLE 0.5ms"));
            m_timer_btn_->setStyleSheet(QString(
                "QPushButton { background-color: %1; color: #06121F; border: none;"
                " padding: 10px 22px; font-weight: 700; }").arg(theme::Colors::ACCENT_NEON));
        }
    } else {
        releaseTimerResolution();
        active = false;
        m_timer_btn_->setText(QString::fromUtf8("\xD0\x92\xD0\x9A\xD0\x9B"));
        m_timer_btn_->setStyleSheet("");
    }
    m_timer_label_->setText(QString::fromUtf8(
        "\xD0\xA2%D0%B0%D0%B9%D0%BC%D0%B5%D1%80\x3A\x20%1 ms")
        .arg(currentTimerResolution() / 10000.0, 0, 'f', 2));
}

void FpsBoostWidget::onRamClean() {
    m_ram_btn_->setEnabled(false);
    QApplication::processEvents();

    auto stats = cleanRam();

    double before_mb = (stats.total_physical - stats.avail_physical + stats.bytes_freed_estimate) / (1024.0 * 1024);
    double after_mb = (stats.total_physical - stats.avail_physical) / (1024.0 * 1024);

    m_ram_label_->setText(
        QString::fromUtf8("\xD0\x9E\xD1\x81%D0%B2%D0%BE%D0%B1%D0%BE%D0%B6%D0%B4%D0%B5%D0%BD%D0\xBE\x3A\x20%1 MB"
                          "\x20\x28\xD0\xBF\xD1%80\xD0\xBE%D1%86\xD0%B5\xD1\x81\xD1%81\xD0%BE%D0%B2\x3A\x20%2\x29")
            .arg(static_cast<int>(stats.bytes_freed_estimate / (1024 * 1024)))
            .arg(stats.processes_trimmed));
    m_ram_btn_->setEnabled(true);
}

void FpsBoostWidget::onServiceToggle() {}

void FpsBoostWidget::onStartupToggle(int row, int col) {
    if (col != 2) return;
    auto item = m_startup_table_->item(row, 0);
    if (!item) return;

    auto entries = enumStartupPrograms();
    if (row < 0 || row >= static_cast<int>(entries.size())) return;
    const auto& entry = entries[static_cast<std::size_t>(row)];
    setStartupEnabled(entry.location, entry.name, !entry.enabled);
    onRefreshStartup();
}

void FpsBoostWidget::onRefreshServices() {
    const bool elevated = SystemAudit::isAdmin();

    struct SvcDef { const char* name; const char* display; };
    static const SvcDef services[] = {
        {"SysMain", "SysMain (Superfetch)"},
        {"WSearch", "Windows Search"},
        {"DoSvc", "Delivery Optimization"},
    };

    m_services_table_->setRowCount(3);
    for (int i = 0; i < 3; ++i) {
        auto info = queryService(services[i].name);
        if (!elevated) {
            auto* warn = new QTableWidgetItem(
                QString::fromUtf8("\xD0\x9D\xD1\x83\xD0\xB6\xD0\xBD\xD1\x8B\x20\xD0\xBF\xD1%80\xD0%B0%D0%B2\xD0\xB0") + " " + QString::fromUtf8(services[i].display));
            warn->setForeground(QBrush(QColor(theme::Colors::WARNING)));
            m_services_table_->setItem(i, 0, warn);
        } else {
            m_services_table_->setItem(i, 0,
                new QTableWidgetItem(QString::fromUtf8(services[i].display)));
        }
        auto* status_item = new QTableWidgetItem(serviceStatusText(info));
        status_item->setForeground(QBrush(serviceColor(info)));
        m_services_table_->setItem(i, 1, status_item);

        auto* toggle_item = new QTableWidgetItem(
            info.start_type == 4 ? QStringLiteral("Enable")
                                 : QStringLiteral("Disable"));
        m_services_table_->setItem(i, 2, toggle_item);
    }
}

void FpsBoostWidget::onRefreshStartup() {
    auto entries = enumStartupPrograms();
    m_startup_table_->setRowCount(static_cast<int>(entries.size()));
    for (int row = 0; row < static_cast<int>(entries.size()); ++row) {
        const auto& e = entries[static_cast<std::size_t>(row)];
        m_startup_table_->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(e.name)));
        m_startup_table_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(e.command)));
        m_startup_table_->setItem(row, 2, new QTableWidgetItem(e.enabled ? "YES" : "NO"));
    }
    m_startup_table_->resizeRowsToContents();
}

// Helper for table styling (called once per table)
void FpsBoostWidget::m_table_style_helper(QTableWidget*) {} // stub

} // namespace gno
