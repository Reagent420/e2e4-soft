#include "diagnostics_widget.h"
#include "theme.h"
#include "monitoring_service.h"
#include "../core/connection_grader.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QFrame>
#include <QDateTime>
#include <QDir>
#include <QTabBar>

#include "../core/game_detector.h"
#include "../core/speed_test.h"
#include "../core/system_audit.h"
#include "../core/plain_language.h"
#include "../core/report_exporter.h"
#include "../core/json_persistence.h"
#include "report_export.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPointer>
#include <thread>

namespace gno {

namespace {

// Роли комбобокса игр: чистое отображаемое имя и process_name.
// Нельзя брать itemText() — у запущенных игр к нему добавлен суффикс «●».
constexpr int kRoleGameName = Qt::UserRole;
constexpr int kRoleProcess  = Qt::UserRole + 1;

QString statusColor(int severity) {
    return severity == 2 ? theme::Colors::ERROR
                         : severity == 1 ? theme::Colors::WARNING
                                         : theme::Colors::SUCCESS;
}

QString statusIcon(int severity) {
    return severity == 2 ? QString::fromUtf8("✕")
                         : severity == 1 ? QString::fromUtf8("⚠")
                                         : QString::fromUtf8("✓");
}

void clearLayout(QVBoxLayout* layout) {
    QLayoutItem* item;
    while ((item = layout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
}

// Контент секции с собственным скроллом — никаких вложенных скролл-зон.
QWidget* makeScrollHost(QWidget*& contentOut) {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(2, 6, 8, 2);
    layout->setSpacing(8);
    scroll->setWidget(content);
    contentOut = content;
    return scroll;
}

} // namespace

DiagnosticsWidget::DiagnosticsWidget(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    renderCapabilities();

    auto& svc = MonitoringService::instance();
    connect(&svc, &MonitoringService::diagnosticsTriggered,
            this, [this](const QString& game, const QString& process) {
        m_lastGame = game;
        m_lastProcess = process;
        int idx = -1;
        for (int i = 0; i < m_gameCombo_->count(); ++i) {
            if (!process.isEmpty() &&
                m_gameCombo_->itemData(i, kRoleProcess).toString() == process) { idx = i; break; }
            if (m_gameCombo_->itemData(i, kRoleGameName).toString() == game) idx = i;
        }
        if (idx >= 0) m_gameCombo_->setCurrentIndex(idx);
        runDiagnostics(game, process);
    });
}

void DiagnosticsWidget::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 20, 24, 20);
    mainLayout->setSpacing(12);

    // ---------- Заголовок ----------
    auto* titleRow = new QHBoxLayout();
    auto* title = new QLabel(QString::fromUtf8("Диагностика"), this);
    title->setObjectName("sectionTitle");
    titleRow->addWidget(title);
    titleRow->addStretch();
    auto* subtitle = new QLabel(QString::fromUtf8(
        "11 проверок сети, системы и игры · запускается сама при старте игры"), this);
    subtitle->setObjectName("sectionSubtitle");
    titleRow->addWidget(subtitle);
    mainLayout->addLayout(titleRow);

    // ---------- Панель управления ----------
    auto* controlPanel = new QFrame(this);
    controlPanel->setObjectName("settingsGroup");
    auto* cp = new QHBoxLayout(controlPanel);
    cp->setContentsMargins(14, 10, 14, 10);
    cp->setSpacing(10);

    cp->addWidget(new QLabel(QString::fromUtf8("Игра:"), controlPanel));
    m_gameCombo_ = new QComboBox(controlPanel);
    m_gameCombo_->setMinimumWidth(220);
    GameDetector detector;
    auto games = detector.getSupportedGames();
    m_gameCombo_->addItem(QString::fromUtf8("— без конкретной игры —"), QString());
    m_gameCombo_->setItemData(0, QString(), kRoleGameName);
    m_gameCombo_->setItemData(0, QString(), kRoleProcess);
    for (const auto& g : games) {
        const int i = m_gameCombo_->count();
        QString text = QString::fromStdString(g.name);
        if (g.is_running) text += QString::fromUtf8("  ●");
        m_gameCombo_->addItem(text, QString::fromStdString(g.process_name));
        m_gameCombo_->setItemData(i, QString::fromStdString(g.name), kRoleGameName);
        m_gameCombo_->setItemData(i, QString::fromStdString(g.process_name), kRoleProcess);
    }
    cp->addWidget(m_gameCombo_);

    m_runBtn_ = new QPushButton(QString::fromUtf8("▶  Запустить диагностику"), controlPanel);
    m_runBtn_->setObjectName("boostButton");
    m_runBtn_->setFixedHeight(40);
    m_runBtn_->setMinimumWidth(220);
    m_runBtn_->setCursor(Qt::PointingHandCursor);
    cp->addWidget(m_runBtn_);

    m_exportBtn_ = new QPushButton(QString::fromUtf8("Сохранить отчёт (JSON)"), controlPanel);
    m_exportBtn_->setObjectName("boostButton");
    m_exportBtn_->setFixedHeight(40);
    m_exportBtn_->setEnabled(false);
    cp->addWidget(m_exportBtn_);
    cp->addStretch();
    mainLayout->addWidget(controlPanel);

    // ---------- Полоса прогресса (видна только во время работы) ----------
    m_progress_ = new QProgressBar(this);
    m_progress_->setRange(0, 0);
    m_progress_->setTextVisible(false);
    m_progress_->setFixedHeight(6);
    m_progress_->setVisible(false);
    mainLayout->addWidget(m_progress_);

    m_runningLabel_ = new QLabel(QString::fromUtf8(
        "Ещё не запускалась. Нажми «Запустить диагностику» — будет понятно по шагам, что происходит."), this);
    m_runningLabel_->setObjectName("sectionSubtitle");
    m_runningLabel_->setWordWrap(true);
    mainLayout->addWidget(m_runningLabel_);

    bool admin = SystemAudit::isAdmin();
    m_adminLabel_ = new QLabel(admin
        ? QString::fromUtf8("✓ Права администратора есть — все исправления доступны.")
        : QString::fromUtf8("⚠ Нет прав администратора: MTU, DNS и план питания будут недоступны. "
                            "Перезапусти программу «от имени администратора», если захочешь их чинить."), this);
    m_adminLabel_->setStyleSheet(admin
        ? "color:#22c55e; font-size:12px; background:transparent;"
        : "color:#f59e0b; font-size:12px; background:transparent;");
    m_adminLabel_->setWordWrap(true);
    mainLayout->addWidget(m_adminLabel_);

    // ---------- Внутренние вкладки вместо трёх скроллов друг на друге ----------
    m_innerTabs_ = new QTabWidget(this);
    m_innerTabs_->setObjectName("diagTabs");
    m_innerTabs_->tabBar()->setExpanding(false);

    // Вкладка 1: результаты
    QWidget* resultsContent = nullptr;
    auto* resultsTab = makeScrollHost(resultsContent);
    m_resultsList_ = resultsContent;
    m_innerTabs_->addTab(resultsTab, QString::fromUtf8("Результаты проверок"));

    m_summaryLabel_ = new QLabel(QString::fromUtf8(
        "Здесь появятся результаты: сначала ошибки, потом предупреждения, потом всё, что в порядке."), m_resultsList_);
    m_summaryLabel_->setObjectName("gameTitle");
    m_summaryLabel_->setWordWrap(true);
    qobject_cast<QVBoxLayout*>(m_resultsList_->layout())->addWidget(m_summaryLabel_);

    // Вкладка 2: проблемы
    auto* problemsWrap = new QWidget;
    auto* pw = new QVBoxLayout(problemsWrap);
    pw->setContentsMargins(2, 8, 2, 2);
    pw->setSpacing(8);
    m_problemSearch_ = new QLineEdit(problemsWrap);
    m_problemSearch_->setObjectName("searchBox");
    m_problemSearch_->setPlaceholderText(QString::fromUtf8("Поиск по симптомам и решениям…"));
    m_problemSearch_->setClearButtonEnabled(true);
    pw->addWidget(m_problemSearch_);
    QWidget* problemsContent = nullptr;
    pw->addWidget(makeScrollHost(problemsContent), 1);
    m_problemsList_ = problemsContent;
    m_innerTabs_->addTab(problemsWrap, QString::fromUtf8("Частые проблемы"));

    // Вкладка 3: что может программа
    QWidget* capsContent = nullptr;
    auto* capsTab = makeScrollHost(capsContent);
    m_capabilitiesList_ = capsContent;
    m_innerTabs_->addTab(capsTab, QString::fromUtf8("Что может программа"));

    // Вкладка 4: простым языком
    auto* plainWrap = new QWidget;
    auto* pl = new QVBoxLayout(plainWrap);
    pl->setContentsMargins(2, 8, 2, 2);
    auto* plainBtn = new QPushButton(QString::fromUtf8("Собрать объяснение простым языком"), plainWrap);
    plainBtn->setObjectName("boostButton");
    plainBtn->setFixedHeight(36);
    plainBtn->setMaximumWidth(320);
    pl->addWidget(plainBtn, 0, Qt::AlignLeft);
    m_plainText_ = new QTextEdit(plainWrap);
    m_plainText_->setReadOnly(true);
    pl->addWidget(m_plainText_, 1);
    m_innerTabs_->addTab(plainWrap, QString::fromUtf8("Простым языком"));

    // Вкладка 5: маршрут до сервера игры (DiagnosticsService)
    setupRouteTab();

    mainLayout->addWidget(m_innerTabs_, 1);

    // ---------- Сигналы ----------
    connect(m_runBtn_, &QPushButton::clicked, this, [this]() {
        const int i = m_gameCombo_->currentIndex();
        runDiagnostics(m_gameCombo_->itemData(i, kRoleGameName).toString(),
                       m_gameCombo_->itemData(i, kRoleProcess).toString());
    });
    connect(m_exportBtn_, &QPushButton::clicked, this, [this]() {
        std::vector<ReportExporter::Check> checks;
        if (m_hasDiag_)
            for (const auto& c : m_lastDiag_.checks)
                checks.push_back({c.name, c.severity});
        auto& svc = MonitoringService::instance();
        const int score = static_cast<int>(
            ConnectionGrader::evaluate(svc.currentPing(), svc.currentJitter(),
                                       svc.currentLossPercent()).score);
        const QString reportGame = m_hasDiag_ && !m_lastDiag_.game_name.empty()
            ? QString::fromStdString(m_lastDiag_.game_name)
            : QStringLiteral("system");
        const std::string json = ReportExporter::build(
            reportGame.toStdString(),
            svc.currentPing(), svc.currentJitter(), svc.currentLossPercent(), score,
            {}, -1, checks);
        const QString dir = qEnvironmentVariable("APPDATA") + QStringLiteral("/GNO/Reports");
        QDir().mkpath(dir);
        const QString path = dir + QStringLiteral("/report_%1.json")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
        persistence::atomicWriteText(path.toStdString(), json);
        m_runningLabel_->setText(QString::fromUtf8("✓ Отчёт сохранён: ") + path);
    });
    connect(plainBtn, &QPushButton::clicked, this, &DiagnosticsWidget::onPlainLanguageClicked);
    connect(m_problemSearch_, &QLineEdit::textChanged, this, &DiagnosticsWidget::onSearchChanged);
    connect(m_gameCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DiagnosticsWidget::onGameSelected);
    connect(m_routeBtn_, &QPushButton::clicked, this, &DiagnosticsWidget::runRouteDiagnostics);
    connect(m_routeCancelBtn_, &QPushButton::clicked, this, &DiagnosticsWidget::cancelRouteDiagnostics);
    connect(m_routeExportBtn_, &QPushButton::clicked, this, &DiagnosticsWidget::exportRouteReport);
}

void DiagnosticsWidget::setupRouteTab()
{
    // Диагностика маршрута требует запущенной игры: сервис ищет её процесс,
    // перечисляет его соединения и измеряет шлюз + прямой маршрут. Без игры
    // честно говорим, что анализировать нечего.
    auto* wrap = new QWidget;
    auto* layout = new QVBoxLayout(wrap);
    layout->setContentsMargins(2, 8, 2, 2);
    layout->setSpacing(8);

    auto* buttonRow = new QHBoxLayout();
    m_routeBtn_ = new QPushButton(QString::fromUtf8("▶  Проверить маршрут до сервера"), wrap);
    m_routeBtn_->setObjectName("boostButton");
    m_routeBtn_->setFixedHeight(36);
    m_routeBtn_->setCursor(Qt::PointingHandCursor);
    buttonRow->addWidget(m_routeBtn_);

    m_routeCancelBtn_ = new QPushButton(QString::fromUtf8("Отменить"), wrap);
    m_routeCancelBtn_->setFixedHeight(36);
    m_routeCancelBtn_->setVisible(false);
    m_routeCancelBtn_->setCursor(Qt::PointingHandCursor);
    buttonRow->addWidget(m_routeCancelBtn_);

    // Экспорт отчёта: пока диагноза нет — кнопка выключена, чтобы не сохранять
    // пустышку. Раньше отчёты умели только PNG, а диагноз не выгружался вообще.
    m_routeExportBtn_ = new QPushButton(QString::fromUtf8("Экспорт отчёта…"), wrap);
    m_routeExportBtn_->setFixedHeight(36);
    m_routeExportBtn_->setEnabled(false);
    m_routeExportBtn_->setCursor(Qt::PointingHandCursor);
    buttonRow->addWidget(m_routeExportBtn_);

    buttonRow->addStretch();
    layout->addLayout(buttonRow);

    m_routeStatusLabel_ = new QLabel(QString::fromUtf8(
        "Нужно выбрать игру сверху и запустить её. Программа найдёт соединение игры с сервером, "
        "измерит шлюз и прямой маршрут и объяснит, виновата ли локальная сеть."), wrap);
    m_routeStatusLabel_->setObjectName("sectionSubtitle");
    m_routeStatusLabel_->setWordWrap(true);
    layout->addWidget(m_routeStatusLabel_);

    m_routeOutcomeLabel_ = new QLabel(wrap);
    m_routeOutcomeLabel_->setObjectName("gameTitle");
    m_routeOutcomeLabel_->setWordWrap(true);
    m_routeOutcomeLabel_->setVisible(false);
    layout->addWidget(m_routeOutcomeLabel_);

    m_routeEvidence_ = new QTextEdit(wrap);
    m_routeEvidence_->setReadOnly(true);
    m_routeEvidence_->setVisible(false);
    layout->addWidget(m_routeEvidence_, 1);

    m_innerTabs_->addTab(wrap, QString::fromUtf8("Маршрут до сервера"));
}

void DiagnosticsWidget::setRouteRunningUi(bool running)
{
    m_routeBtn_->setEnabled(!running);
    m_routeBtn_->setText(running ? QString::fromUtf8("⏳ Идёт проверка маршрута…")
                                 : QString::fromUtf8("▶  Проверить маршрут до сервера"));
    m_routeCancelBtn_->setVisible(running);
    // Во время нового прогона старый отчёт устарел — не даём сохранить его
    // как свежий.
    if (running) {
        m_hasRouteReport_ = false;
        if (m_routeExportBtn_) m_routeExportBtn_->setEnabled(false);
    }
}

void DiagnosticsWidget::runRouteDiagnostics()
{
    if (m_routeRunning_.exchange(true))
        return;

    const int i = m_gameCombo_->currentIndex();
    const QString game = m_gameCombo_->itemData(i, kRoleGameName).toString();
    if (game.isEmpty()) {
        m_routeRunning_.store(false);
        m_routeStatusLabel_->setText(QString::fromUtf8(
            "Выбери конкретную игру в списке наверху — диагностика маршрута работает "
            "по соединению запущенного игрового процесса."));
        return;
    }

    // Свежий источник отмены на каждый запуск: старый токен мог остаться
    // у давно завершившегося потока.
    m_routeCancel_ = CancellationSource();
    setRouteRunningUi(true);
    m_routeStatusLabel_->setText(QString::fromUtf8(
        "Ищем процесс игры и его соединение, затем измеряем шлюз и прямой маршрут… ~10 секунд."));
    m_routeOutcomeLabel_->setVisible(false);
    m_routeEvidence_->setVisible(false);

    const CancellationToken token = m_routeCancel_.token();
    QPointer<DiagnosticsWidget> self(this);
    std::thread([self, game, token]() {
        DiagnosticResult<DiagnosticReport> result;
        try {
            DiagnosticsService service;
            ProbeRequest request;
            request.game_id = game.toStdString();
            request.duration_seconds = 10;
            result = service.run(request, token);
        } catch (const std::exception& e) {
            if (!self) return;
            const QString msg = QString::fromUtf8("✗ Диагностика маршрута завершилась ошибкой: ") +
                                QString::fromUtf8(e.what());
            QMetaObject::invokeMethod(self, [self, msg]() {
                if (!self) return;
                self->m_routeRunning_.store(false);
                self->setRouteRunningUi(false);
                self->m_routeStatusLabel_->setText(msg);
            }, Qt::QueuedConnection);
            return;
        } catch (...) {
            if (!self) return;
            QMetaObject::invokeMethod(self, [self]() {
                if (!self) return;
                self->m_routeRunning_.store(false);
                self->setRouteRunningUi(false);
                self->m_routeStatusLabel_->setText(QString::fromUtf8(
                    "✗ Диагностика маршрута завершилась неизвестной ошибкой."));
            }, Qt::QueuedConnection);
            return;
        }

        if (!self) return;
        const bool ok = result.ok();
        const DiagnosticReport report = result.value;
        const DiagnosticError error = result.error;
        QMetaObject::invokeMethod(self, [self, ok, report, error]() {
            if (!self) return;
            self->m_routeRunning_.store(false);
            self->setRouteRunningUi(false);
            if (ok) {
                self->renderRouteReport(report);
                // Результат диагностики становится рекомендацией в строке
                // состояния и в трее — раньше recommendationAvailable
                // эмитился только по истории прошлых сессий.
                MonitoringService::instance().applyDiagnosticReport(report);
            } else {
                self->renderRouteError(error, report.evidence);
            }
        }, Qt::QueuedConnection);
    }).detach();
}

void DiagnosticsWidget::cancelRouteDiagnostics()
{
    // Токен разделяется с рабочим потоком: сервис проверяет его между этапами
    // и прерывается, не дожидаясь полного окна измерений.
    m_routeCancel_.cancel();
    m_routeStatusLabel_->setText(QString::fromUtf8("Отмена… досрочно останавливаем измерения."));
}

void DiagnosticsWidget::renderRouteReport(const DiagnosticReport& report)
{
    std::vector<std::string> evidence;
    const std::string headline = PlainLanguageReport::formatDiagnosticReport(report, &evidence);

    m_routeOutcomeLabel_->setText(QString::fromStdString(headline));
    const bool bad = report.outcome == DiagnosticOutcome::LocalNetworkProblem ||
                     report.outcome == DiagnosticOutcome::ImprovementLikely;
    m_routeOutcomeLabel_->setStyleSheet(
        QString("color:%1; font-size:14px; font-weight:700; background:transparent;")
            .arg(bad ? theme::Colors::WARNING : theme::Colors::SUCCESS));
    m_routeOutcomeLabel_->setVisible(true);

    QString text;
    for (const auto& line : evidence)
        text += QString::fromUtf8("· ") + QString::fromStdString(line) + "\n";
    m_routeEvidence_->setPlainText(text);
    m_routeEvidence_->setVisible(!text.isEmpty());

    const QString recommendation = QString::fromStdString(
        PlainLanguageReport::diagnosticRecommendation(report));
    if (!recommendation.isEmpty())
        m_routeStatusLabel_->setText(recommendation);
    else
        m_routeStatusLabel_->setText(QString::fromUtf8("Диагноз собран, но данных маловато для рекомендации."));

    // Запоминаем отчёт для экспорта и включаем кнопку — теперь есть что выгружать.
    m_lastRouteReport_ = report;
    m_hasRouteReport_ = true;
    if (m_routeExportBtn_) m_routeExportBtn_->setEnabled(true);

    m_innerTabs_->setCurrentIndex(4);
}

void DiagnosticsWidget::exportRouteReport()
{
    if (!m_hasRouteReport_) return;

    const QString dir = report::defaultReportsDir();
    QDir().mkpath(dir);

    QFileDialog dialog(this, QString::fromUtf8("Сохранить отчёт диагностики"));
    dialog.setDirectory(dir);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setNameFilters({
        QString::fromUtf8("Markdown (*.md)"),
        QString::fromUtf8("HTML (*.html)"),
        QString::fromUtf8("PDF (*.pdf)"),
    });
    dialog.selectFile(QString::fromUtf8("diagnostics-%1.md")
                          .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd-HHmm")));
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
        return;

    const QString path = dialog.selectedFiles().first();
    report::ReportFormat format = report::ReportFormat::Markdown;
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == "html" || suffix == "htm") format = report::ReportFormat::Html;
    else if (suffix == "pdf") format = report::ReportFormat::Pdf;

    if (report::exportDiagnosticReport(path, m_lastRouteReport_, format)) {
        QMessageBox::information(this, QString::fromUtf8("Отчёт сохранён"),
                                 QString::fromUtf8("Файл записан:\n%1").arg(path));
    } else {
        QMessageBox::warning(this, QString::fromUtf8("Не удалось сохранить"),
                             QString::fromUtf8("Не получилось записать файл:\n%1").arg(path));
    }
}

void DiagnosticsWidget::renderRouteError(DiagnosticError error,
                                         const std::vector<std::string>& evidence)
{
    m_routeOutcomeLabel_->setText(QString::fromUtf8("✗ ") +
        QString::fromStdString(PlainLanguageReport::errorLabel(error)));
    m_routeOutcomeLabel_->setStyleSheet(
        QString("color:%1; font-size:14px; font-weight:700; background:transparent;")
            .arg(theme::Colors::ERROR));
    m_routeOutcomeLabel_->setVisible(true);

    QString text;
    for (const auto& line : evidence)
        text += QString::fromUtf8("· ") + QString::fromStdString(line) + "\n";
    m_routeEvidence_->setPlainText(text);
    m_routeEvidence_->setVisible(!text.isEmpty());

    // GameNotDetected — самый частый случай: игра не запущена. Подсказываем,
    // что делать, вместо сухого «ошибка».
    m_routeStatusLabel_->setText(error == DiagnosticError::GameNotDetected
        ? QString::fromUtf8("Запусти выбранную игру и повтори проверку — анализ идёт по её живому соединению.")
        : QString::fromUtf8("Проверку не удалось завершить. Попробуй ещё раз или выбери другую игру."));
}

void DiagnosticsWidget::setRunningUi(bool running)
{
    m_progress_->setVisible(running);
    m_runBtn_->setEnabled(!running);
    m_runBtn_->setText(running ? QString::fromUtf8("⏳ Идёт проверка…")
                               : QString::fromUtf8("▶  Запустить диагностику"));
}

void DiagnosticsWidget::onGameSelected(int index)
{
    const QString game = m_gameCombo_->itemData(index, kRoleGameName).toString();
    renderProblems(game);
}

QWidget* DiagnosticsWidget::makeCheckCard(const DiagnosticCheck& c)
{
    auto* card = new QFrame(m_resultsList_);
    card->setObjectName("gameCard");
    card->setStyleSheet(QString("#gameCard { border-left: 4px solid %1; }").arg(statusColor(c.severity)));

    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 10, 14, 10);
    layout->setSpacing(4);

    auto* head = new QHBoxLayout();
    auto* iconLbl = new QLabel(statusIcon(c.severity), card);
    iconLbl->setStyleSheet(QString("color:%1; font-size:18px; font-weight:700; background:transparent;")
                               .arg(statusColor(c.severity)));
    head->addWidget(iconLbl);

    auto* nameLbl = new QLabel(QString::fromStdString(c.name), card);
    nameLbl->setObjectName("gameTitle");
    head->addWidget(nameLbl);

    auto* catLbl = new QLabel(QString::fromStdString(c.category), card);
    catLbl->setObjectName("gameCategory");
    head->addWidget(catLbl);
    head->addStretch();

    auto* verLbl = new QLabel(c.severity == 0 ? QString::fromUtf8("в порядке")
                               : c.severity == 1 ? QString::fromUtf8("внимание")
                                                 : QString::fromUtf8("мешает играть"), card);
    verLbl->setStyleSheet(QString("color:%1; font-size:11px; font-weight:700; background:transparent;")
                              .arg(statusColor(c.severity)));
    head->addWidget(verLbl);
    layout->addLayout(head);

    auto* detailLbl = new QLabel(QString::fromStdString(c.detail), card);
    detailLbl->setObjectName("sectionSubtitle");
    detailLbl->setWordWrap(true);
    layout->addWidget(detailLbl);

    if (!c.explanation.empty()) {
        auto* explainLbl = new QLabel(QString::fromUtf8("Как программа это видит: ") +
                                          QString::fromStdString(c.explanation), card);
        explainLbl->setStyleSheet("color:rgba(255,255,255,0.45); font-size:11px; font-style:italic; background:transparent;");
        explainLbl->setWordWrap(true);
        layout->addWidget(explainLbl);
    }

    if (!c.recommendation.empty()) {
        const QString recColor = c.severity == 2 ? theme::Colors::ERROR
                               : c.severity == 1 ? theme::Colors::WARNING
                                                 : theme::Colors::SUCCESS;
        auto* recLbl = new QLabel(QString::fromUtf8("→ Что сделать: ") +
                                      QString::fromStdString(c.recommendation), card);
        recLbl->setStyleSheet(QString("color:%1; font-size:12px; background:transparent;").arg(recColor));
        recLbl->setWordWrap(true);
        layout->addWidget(recLbl);
    }

    if (!c.passed && !c.fix_action.empty()) {
        auto* fixBtn = new QPushButton(QString::fromUtf8("Исправить автоматически"), card);
        fixBtn->setObjectName("boostButton");
        fixBtn->setFixedHeight(32);
        fixBtn->setCursor(Qt::PointingHandCursor);
        QString action = QString::fromStdString(c.fix_action);
        connect(fixBtn, &QPushButton::clicked, this, [this, action, fixBtn]() {
            fixBtn->setEnabled(false);
            const std::string result = LaunchDiagnostics::applyFix(action.toStdString());
            fixBtn->setEnabled(true);
            m_runningLabel_->setText(QString::fromUtf8("✓ ") + QString::fromStdString(result));
        });
        layout->addWidget(fixBtn, 0, Qt::AlignLeft);
    }

    return card;
}

QWidget* DiagnosticsWidget::makeProblemCard(const ProblemEntry& e)
{
    auto* card = new QFrame(m_problemsList_);
    card->setObjectName("settingsGroup");

    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 10, 14, 10);
    layout->setSpacing(4);

    auto* head = new QHBoxLayout();
    auto* titleLbl = new QLabel(QString::fromStdString(e.title), card);
    titleLbl->setObjectName("gameTitle");
    head->addWidget(titleLbl);
    auto* diffLbl = new QLabel(QString::fromStdString(e.difficulty), card);
    diffLbl->setObjectName("gameCategory");
    head->addWidget(diffLbl);
    head->addStretch();
    layout->addLayout(head);

    auto addRow = [&](const QString& prefix, const std::string& text, const QString& color) {
        auto* lbl = new QLabel(QString::fromUtf8("%1: %2").arg(prefix, QString::fromStdString(text)), card);
        lbl->setStyleSheet(QString("color:%1; font-size:12px; background:transparent;").arg(color));
        lbl->setWordWrap(true);
        layout->addWidget(lbl);
    };

    addRow(QString::fromUtf8("Симптомы"), e.symptoms, "rgba(255,255,255,0.7)");
    addRow(QString::fromUtf8("Причина"), e.cause, "rgba(255,255,255,0.5)");
    addRow(QString::fromUtf8("Решение через программу"), e.solution, theme::Colors::ACCENT_CYAN);

    if (!e.fix_action.empty()) {
        auto* fixBtn = new QPushButton(QString::fromUtf8("Применить решение"), card);
        fixBtn->setObjectName("boostButton");
        fixBtn->setFixedHeight(32);
        fixBtn->setCursor(Qt::PointingHandCursor);
        QString action = QString::fromStdString(e.fix_action);
        connect(fixBtn, &QPushButton::clicked, this, [this, action, fixBtn]() {
            fixBtn->setEnabled(false);
            const std::string result = LaunchDiagnostics::applyFix(action.toStdString());
            fixBtn->setEnabled(true);
            m_runningLabel_->setText(QString::fromUtf8("✓ ") + QString::fromStdString(result));
        });
        layout->addWidget(fixBtn, 0, Qt::AlignLeft);
    }

    return card;
}

void DiagnosticsWidget::renderResults(const GameDiagnostics& diag)
{
    auto* layout = qobject_cast<QVBoxLayout*>(m_resultsList_->layout());
    if (!layout) return;

    // удалить карточки (первая — summary-лейбл, её не трогаем)
    while (layout->count() > 1) {
        auto* item = layout->takeAt(1);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    while (layout->count() > 2) { // и stretch после карточек
        auto* item = layout->takeAt(layout->count() - 1);
        delete item;
    }

    m_summaryLabel_->setText(
        QString::fromUtf8("Итог: %1 проверок — ✓ %2 в порядке · ⚠ %3 внимания · ✕ %4 мешают")
            .arg(diag.checks.size())
            .arg(diag.passed_count)
            .arg(diag.warning_count)
            .arg(diag.error_count));

    for (const auto& c : diag.checks) if (c.severity == 2) layout->addWidget(makeCheckCard(c));
    for (const auto& c : diag.checks) if (c.severity == 1) layout->addWidget(makeCheckCard(c));
    for (const auto& c : diag.checks) if (c.severity == 0) layout->addWidget(makeCheckCard(c));
    layout->addStretch();

    m_innerTabs_->setCurrentIndex(0);
    if (auto* sa = qobject_cast<QScrollArea*>(m_resultsList_->parentWidget()))
        if (sa->verticalScrollBar()) sa->verticalScrollBar()->setValue(sa->verticalScrollBar()->minimum());
}

void DiagnosticsWidget::renderProblems(const QString& gameName)
{
    m_lastGameName = gameName;

    auto* layout = qobject_cast<QVBoxLayout*>(m_problemsList_->layout());
    if (!layout) return;
    clearLayout(layout);

    if (gameName.isEmpty()) {
        auto* hint = new QLabel(QString::fromUtf8(
            "Выбери игру в списке наверху — здесь появятся её частые проблемы и решения."), m_problemsList_);
        hint->setObjectName("sectionSubtitle");
        hint->setWordWrap(true);
        layout->addWidget(hint);
        layout->addStretch();
        return;
    }

    auto problems = ProblemDb::search(gameName.toStdString(), m_problemQuery_.toStdString());
    if (problems.empty()) {
        auto* hint = new QLabel(QString::fromUtf8("Для этой игры пока нет записей в базе решений."), m_problemsList_);
        hint->setObjectName("sectionSubtitle");
        layout->addWidget(hint);
        layout->addStretch();
        return;
    }

    for (const auto& e : problems)
        layout->addWidget(makeProblemCard(e));
    layout->addStretch();
}

void DiagnosticsWidget::renderCapabilities()
{
    auto* layout = qobject_cast<QVBoxLayout*>(m_capabilitiesList_->layout());
    if (!layout) return;
    clearLayout(layout);

    auto caps = SystemAudit::getCapabilities();
    for (const auto& c : caps) {
        auto* card = new QFrame(m_capabilitiesList_);
        card->setObjectName("gameCard");

        auto* row = new QHBoxLayout(card);
        row->setContentsMargins(12, 8, 12, 8);
        row->setSpacing(12);

        QColor statColor = c.currently_possible ? QColor(theme::Colors::SUCCESS)
                             : c.requires_vpn_server ? QColor(theme::Colors::TEXT_TERTIARY)
                                                     : QColor(theme::Colors::WARNING);
        const QString icon = c.currently_possible ? QString::fromUtf8("✓")
                               : c.requires_vpn_server ? QString::fromUtf8("—")
                                                       : QString::fromUtf8("⚠");
        auto* iconLbl = new QLabel(icon, card);
        iconLbl->setStyleSheet(QString("color:%1; font-size:15px; font-weight:700; background:transparent;")
                                   .arg(statColor.name()));
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
        statusLbl->setStyleSheet(QString("color:%1; font-size:11px; font-weight:600; background:transparent;")
                                     .arg(statColor.name()));
        textLayout->addWidget(statusLbl);

        row->addLayout(textLayout, 1);
        layout->addWidget(card);
    }
    layout->addStretch();
}

void DiagnosticsWidget::runDiagnostics(const QString& gameName, const QString& processName)
{
    if (m_running_.exchange(true))
        return;

    m_lastGame = gameName;
    m_lastProcess = processName;
    setRunningUi(true);
    m_innerTabs_->setCurrentIndex(0);
    m_runningLabel_->setText(QString::fromUtf8(
        "Идёт проверка: интернет → DNS → MTU → план питания → DVR → диск → память → процессы → библиотека игры. "
        "Затем замер задержки до регионов EU/NA/ASIA. ~10–20 секунд…"));

    std::string game = gameName.toStdString();
    std::string proc = processName.toStdString();

    QPointer<DiagnosticsWidget> self(this);
    std::thread([self, game, proc]() {
        GameDiagnostics diag;
        QString error;
        QString region_advice;
        try {
            diag = LaunchDiagnostics::run(game, proc);

            SpeedTest st;
            struct R { const char* name; const char* ip; } regions[] = {
                {"EU", "1.1.1.1"}, {"NA", "104.16.132.229"}, {"ASIA", "103.2.131.17"}};
            double best = 1e9; const char* best_name = nullptr; int best_ms = -1;
            for (auto& r : regions) {
                auto res = st.benchmarkServer(r.ip, 3);
                if (!res.success) continue;
                if (res.latency_ms < best) {
                    best = res.latency_ms;
                    best_name = r.name;
                    best_ms = static_cast<int>(res.latency_ms);
                }
            }
            if (best_name)
                region_advice = QString::fromUtf8("Ближайший регион: ") +
                    QString::fromUtf8(best_name) + QStringLiteral(" (%1 мс)").arg(best_ms);
        } catch (const std::exception& e) {
            error = QString::fromUtf8("✗ Диагностика завершилась ошибкой: ") + QString::fromUtf8(e.what());
        } catch (...) {
            error = QString::fromUtf8("✗ Диагностика завершилась неизвестной ошибкой.");
        }

        if (!self) return;
        QMetaObject::invokeMethod(self, [self, diag, region_advice, error]() {
            if (!self) return;
            self->m_running_.store(false);
            self->setRunningUi(false);
            if (!error.isEmpty()) {
                self->m_runningLabel_->setText(error);
                return;
            }
            self->m_lastDiag_ = diag;
            self->m_hasDiag_ = true;
            if (self->m_exportBtn_) self->m_exportBtn_->setEnabled(true);
            self->renderResults(diag);
            QString verdict;
            if (diag.error_count > 0)
                verdict = QString::fromUtf8("Найдено %1 проблем, которые мешают игре — они первыми в списке. "
                                            "У большинства есть кнопка «Исправить автоматически».").arg(diag.error_count);
            else if (diag.warning_count > 0)
                verdict = QString::fromUtf8("Серьёзных проблем нет. %1 предупреждений — не блокируют, но могут мешать.").arg(diag.warning_count);
            else
                verdict = QString::fromUtf8("Всё в порядке — система и сеть к игре готовы.");
            if (!region_advice.isEmpty())
                verdict += QStringLiteral("  ·  ") + region_advice;
            self->m_runningLabel_->setText(verdict);
        }, Qt::QueuedConnection);
    }).detach();
}

void DiagnosticsWidget::onSearchChanged(const QString& text)
{
    m_problemQuery_ = text;
    renderProblems(m_lastGameName);
}

void DiagnosticsWidget::onPlainLanguageClicked()
{
    auto& svc = MonitoringService::instance();
    const bool net_ok = svc.hasPing() && svc.currentLossPercent() < 5.0;
    auto sections = PlainLanguageReport::build(
        svc.currentPing(), svc.currentJitter(), svc.currentLossPercent(), net_ok,
        m_hasDiag_ ? &m_lastDiag_ : nullptr, SystemAudit::isAdmin());

    QString out;
    for (const auto& s : sections) {
        out += QString::fromUtf8("=== ") + QString::fromStdString(s.title) + QString::fromUtf8(" ===\n");
        for (const auto& l : s.lines)
            out += QString::fromUtf8("· ") + QString::fromStdString(l) + "\n";
        out += "\n";
    }
    if (out.isEmpty())
        out = QString::fromUtf8("Пока нечего объяснять — сначала запусти диагностику.");
    m_plainText_->setPlainText(out);
    m_innerTabs_->setCurrentIndex(3);
}

} // namespace gno
