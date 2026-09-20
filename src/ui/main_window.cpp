#include "main_window.h"
#include "sidebar.h"
#include "theme.h"
#include "tabbed_section.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QWidget>
#include <QDialog>
#include <QDialogButtonBox>

#include <cstdlib>

#include "dashboard.h"
#include "game_list.h"
#include "game_profiles_widget.h"
#include "monitoring.h"
#include "optimizer.h"
#include "network_tools.h"
#include "process_monitor_widget.h"
#include "session_history_widget.h"
#include "diagnostics_widget.h"
#include "multipath_widget.h"
#include "remediation_widget.h"
#include "geo_map.h"
#include "fine_tune_widget.h"
#include "knowledge_widget.h"
#include "settings_page.h"

namespace gno {

namespace {

// Отладка и скриншоты: GNO_START_TAB=<n> открывает нужную вкладку секции.
void applyStartTab(TabbedSection* section) {
    const char* env = std::getenv("GNO_START_TAB");
    if (!env) return;
    const int index = std::atoi(env);
    if (index > 0) section->setCurrentTab(index);
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setupUi();
    setupPages();

    connect(m_sidebar, &Sidebar::navigationChanged,
            this, &MainWindow::onNavigationChanged);
    connect(m_sidebar, &Sidebar::settingsRequested,
            this, &MainWindow::openSettings);

    // GNO_START_PAGE=<0..7> — открыть раздел сразу (отладка/скриншоты).
    int start_page = 0;
    if (const char* env = std::getenv("GNO_START_PAGE")) {
        const int parsed = std::atoi(env);
        if (parsed >= 0 && parsed < static_cast<int>(m_page_creators_.size()))
            start_page = parsed;
    }
    m_sidebar->setNavigationIndex(start_page);
    // setNavigationIndex только подсвечивает кнопку — страницу переключаем явно.
    if (start_page != 0) onNavigationChanged(start_page);
}

void MainWindow::setupUi()
{
    setWindowTitle(theme::APP_NAME);
    setMinimumSize(900, 600);
    resize(1200, 750);
    setObjectName("GNO");

    auto* centralWidget = new QWidget(this);
    auto* mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_sidebar = new Sidebar(this);
    mainLayout->addWidget(m_sidebar);

    m_stackedWidget = new QStackedWidget(this);
    m_stackedWidget->setObjectName("mainStack");
    mainLayout->addWidget(m_stackedWidget);

    setCentralWidget(centralWidget);

    auto* statusBarWidget = new QWidget(this);
    statusBarWidget->setObjectName("statusBar");
    auto* statusBarLayout = new QHBoxLayout(statusBarWidget);
    statusBarLayout->setContentsMargins(16, 8, 16, 8);

    m_statusLabel = new QLabel(QString::fromUtf8("Готово к работе"), statusBarWidget);
    m_statusLabel->setObjectName("statusDisconnected");
    statusBarLayout->addWidget(m_statusLabel);
    statusBarLayout->addStretch();

    auto metricStyle = QString("color: rgba(255,255,255,0.55); font-size: 11px; background: transparent;");

    m_pingLabel = new QLabel(QString::fromUtf8("Пинг: --"), statusBarWidget);
    m_pingLabel->setStyleSheet(metricStyle);
    statusBarLayout->addWidget(m_pingLabel);
    statusBarLayout->addSpacing(12);

    m_jitterLabel = new QLabel(QString::fromUtf8("Джиттер: --"), statusBarWidget);
    m_jitterLabel->setStyleSheet(metricStyle);
    statusBarLayout->addWidget(m_jitterLabel);
    statusBarLayout->addSpacing(12);

    m_lossLabel = new QLabel(QString::fromUtf8("Потери: --"), statusBarWidget);
    m_lossLabel->setStyleSheet(metricStyle);
    statusBarLayout->addWidget(m_lossLabel);
    statusBarLayout->addSpacing(12);

    m_boostLabel = new QLabel(QString::fromUtf8("ОПТИМИЗАЦИЯ: ВЫКЛ"), statusBarWidget);
    m_boostLabel->setObjectName("boostTag");
    statusBarLayout->addWidget(m_boostLabel);
    statusBarLayout->addSpacing(8);

    auto* hint = new QLabel(QString::fromUtf8("E2E4 Soft — оптимизация игровой сети"), statusBarWidget);
    hint->setStyleSheet("color: rgba(255,255,255,0.35); font-size: 11px; background: transparent;");
    statusBarLayout->addWidget(hint);

    setStatusBar(new QStatusBar(this));
    statusBar()->setStyleSheet(
        QString("QStatusBar { background-color: %1; border-top: 1px solid %2; }")
            .arg(theme::Colors::BG_SURFACE, theme::Colors::BORDER));
    statusBar()->addWidget(statusBarWidget, 1);
}

// Новая IA (GNO_CONCEPT): 8 секций, все страницы ленивые;
// бывшие 14 пунктов — представлениями (вкладками) внутри секций.
void MainWindow::setupPages()
{
    m_page_creators_.resize(8);
    m_page_created_.assign(8, false);

    m_page_creators_[0] = [this]() {
        auto* dash = new DashboardWidget(this);
        // Главная кнопка буста раньше эмитила сигнал в никуда: индикатор
        // обновлялся, но реальные оптимизации не применялись. Теперь запрос
        // уходит в main_gui → BoostGate (как из трея).
        connect(dash, &DashboardWidget::boostToggled,
                this, &MainWindow::boostRequested);
        return dash;
    };

    m_page_creators_[1] = [this]() {
        auto* sec = new TabbedSection(this);
        sec->addTab(QString::fromUtf8("Игры"), [this]() { return new GameListWidget(this); });
        sec->addTab(QString::fromUtf8("Профили игр"), [this]() { return new GameProfilesWidget(this); });
        return sec;
    };

    m_page_creators_[2] = [this]() {
        auto* sec = new TabbedSection(this);
        sec->addTab(QString::fromUtf8("Live-мониторинг"), [this]() { return new MonitoringWidget(this); });
        sec->addTab(QString::fromUtf8("Тесты сети"), [this]() { return new NetworkToolsWidget(this); });
        sec->addTab(QString::fromUtf8("Диагностика"), [this]() { return new DiagnosticsWidget(this); });
        // MultipathEngine раньше был доступен только из консоли (--multipath).
        // Теперь та же модель — в GUI: таблица путей, подсветка активного,
        // переключатель auto_switch и уведомление о переключении.
        sec->addTab(QString::fromUtf8("Мультипуть"), [this]() {
            auto* mp = new MultipathWidget(this);
            connect(mp, &MultipathWidget::pathSwitched, this, &MainWindow::showRecommendation);
            return mp;
        });
        return sec;
    };

    m_page_creators_[3] = [this]() {
        auto* sec = new TabbedSection(this);
        sec->addTab(QString::fromUtf8("Процессы"), [this]() { return new ProcessMonitorWidget(this); });
        sec->addTab(QString::fromUtf8("Тонкая настройка"), [this]() { return new FineTuneWidget(this); });
        return sec;
    };

    m_page_creators_[4] = [this]() {
        auto* sec = new TabbedSection(this);
        sec->addTab(QString::fromUtf8("Центр изменений"), [this]() { return new RemediationWidget(this); });
        sec->addTab(QString::fromUtf8("Прайм-сетап"), [this, sec]() {
            auto* opt = new OptimizerWidget(this);
            // Обе вкладки работают с ОДНИМ журналом транзакций. После
            // применения или отката на «Прайм-сетапе» соседняя вкладка обязана
            // перечитать историю: иначе «Центр изменений» показывал бы список
            // без только что созданной транзакции, а её кнопка отката —
            // устаревшую доступность. contentAt(0) вернёт nullptr, если вкладку
            // ещё не открывали, — тогда обновлять нечего.
            if (auto* rem = qobject_cast<RemediationWidget*>(sec->contentAt(0))) {
                connect(opt, &OptimizerWidget::systemStateChanged, rem,
                        &RemediationWidget::refreshJournal);
                // И обратно: фикс, применённый в «Центре изменений», должен
                // включить кнопку отката здесь.
                connect(rem, &RemediationWidget::journalChanged, opt,
                        &OptimizerWidget::refreshExternalState);
            }
            return opt;
        });
        applyStartTab(sec);
        return sec;
    };

    m_page_creators_[5] = [this]() { return new GeoMapWidget(this); };
    m_page_creators_[6] = [this]() { return new SessionHistoryWidget(this); };
    m_page_creators_[7] = [this]() {
        auto* k = new KnowledgeWidget(this);
        connect(k, &KnowledgeWidget::applyRequested, this, [this](const QString&) {
            // deep-link:Knowledge → Optimizations (страница 4)
            m_sidebar->setNavigationIndex(4);
            onNavigationChanged(4);
        });
        return k;
    };

    for (std::size_t i = 0; i < m_page_creators_.size(); ++i)
        m_stackedWidget->addWidget(new QWidget(this)); // placeholders

    ensurePage(0);
    m_stackedWidget->setCurrentIndex(0);
}

void MainWindow::onNavigationChanged(int index)
{
    ensurePage(index);
    m_stackedWidget->setCurrentIndex(index);
}

QWidget* MainWindow::ensurePage(int index)
{
    if (index < 0 || index >= static_cast<int>(m_page_creators_.size()))
        return m_stackedWidget->widget(index);
    if (m_page_created_[static_cast<std::size_t>(index)] || !m_page_creators_[static_cast<std::size_t>(index)])
        return m_stackedWidget->widget(index);
    auto* page = m_page_creators_[static_cast<std::size_t>(index)]();
    auto* old = m_stackedWidget->widget(index);
    const int pos = m_stackedWidget->indexOf(old);
    m_stackedWidget->removeWidget(old);
    delete old;
    m_stackedWidget->insertWidget(pos, page);
    m_page_created_[static_cast<std::size_t>(index)] = true;
    return page;
}

// Настройки — модалка (IA 8+1). Сигналы пробрасываются как раньше.
void MainWindow::openSettings()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QString::fromUtf8("Настройки"));
    dialog.resize(720, 560);

    auto* layout = new QVBoxLayout(&dialog);
    auto* page = new SettingsPageWidget(&dialog);
    layout->addWidget(page, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    layout->addWidget(buttons);

    connect(page, &SettingsPageWidget::themeChanged, this, &MainWindow::themeChanged);
    connect(page, &SettingsPageWidget::overlayChanged, this, &MainWindow::overlaySettingsChanged);
    connect(page, &SettingsPageWidget::soundChanged, this, &MainWindow::soundSettingsChanged);
    connect(page, &SettingsPageWidget::notificationsChanged, this, &MainWindow::notificationsSettingsChanged);

    dialog.exec();
}

void MainWindow::updateLiveMetrics(int pingMs, int jitterMs, double lossPercent)
{
    m_pingLabel->setText(QString::fromUtf8("Пинг: %1 мс").arg(pingMs));
    m_jitterLabel->setText(QString::fromUtf8("Джиттер: %1 мс").arg(jitterMs));
    m_lossLabel->setText(QString::fromUtf8("Потери: %1%").arg(lossPercent, 0, 'f', 1));
    m_statusLabel->setText(QString::fromUtf8("Онлайн"));
    m_statusLabel->setObjectName("statusConnected");
    m_statusLabel->style()->unpolish(m_statusLabel);
    m_statusLabel->style()->polish(m_statusLabel);
}

void MainWindow::setBoostIndicator(bool on)
{
    m_boostLabel->setText(on ? QString::fromUtf8("ОПТИМИЗАЦИЯ: ВКЛ") : QString::fromUtf8("ОПТИМИЗАЦИЯ: ВЫКЛ"));
}

void MainWindow::showRecommendation(const QString& text)
{
    if (!text.isEmpty())
        m_statusLabel->setText(text);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    hide();
    event->ignore();
}

void MainWindow::forceShow()
{
    show();
    raise();
    activateWindow();
}

} // namespace gno
