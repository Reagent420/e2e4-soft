#include "diagnostics_widget.h"
#include "theme.h"
#include "monitoring_service.h"
#include "../core/connection_grader.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QFrame>
#include <QThread>
#include <QDateTime>
#include <QDir>
#include <QPainter>
#include <QFont>

#include "../core/game_detector.h"
#include "../core/speed_test.h"
#include "../core/system_audit.h"
#include "../core/plain_language.h"
#include "../core/report_exporter.h"
#include "../core/json_persistence.h"
#include <QDateTime>
#include <QDir>
#include <QTextEdit>

namespace gno {

namespace {

QString statusColor(int severity) {
    return severity == 2 ? theme::Colors::ERROR
                         : severity == 1 ? theme::Colors::WARNING
                                         : theme::Colors::SUCCESS;
}

QString statusIcon(int severity) {
    return severity == 2 ? QString::fromUtf8("?")
                         : severity == 1 ? QString::fromUtf8("?")
                                         : QString::fromUtf8("?");
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
        for (int i = 0; i < m_gameCombo_->count(); ++i) {
            if (m_gameCombo_->itemText(i) == game) {
                m_gameCombo_->setCurrentIndex(i);
                break;
            }
        }
        runDiagnostics(game, process);
    });
}

void DiagnosticsWidget::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(16);

    auto* title = new QLabel(QString::fromUtf8("Диагностика запуска игры"), this);
    title->setObjectName("sectionTitle");
    mainLayout->addWidget(title);

    auto* subtitle = new QLabel(QString::fromUtf8(
        "Программа проверяет систему и сеть перед запуском игры и показывает своими глазами, "
        "что мешает играть. Диагностика запускается автоматически при старте игры."), this);
    subtitle->setObjectName("sectionSubtitle");
    subtitle->setWordWrap(true);
    mainLayout->addWidget(subtitle);

    auto* controlRow = new QHBoxLayout();
    auto* gameLabel = new QLabel(QString::fromUtf8("Игра:"), this);
    m_gameCombo_ = new QComboBox(this);
    m_gameCombo_->setMinimumWidth(240);

    GameDetector detector;
    auto games = detector.getSupportedGames();
    m_gameCombo_->addItem(QString::fromUtf8("— выберите игру —"), QString());
    for (const auto& g : games) {
        m_gameCombo_->addItem(QString::fromStdString(g.name), QString::fromStdString(g.process_name));
        if (g.is_running)
            m_gameCombo_->setItemText(m_gameCombo_->count() - 1,
                QString::fromStdString(g.name) + QString::fromUtf8(" (запущена)"));
    }

    controlRow->addWidget(gameLabel);
    controlRow->addSpacing(12);
    controlRow->addWidget(m_gameCombo_);
    controlRow->addSpacing(8);

    m_runBtn_ = new QPushButton(QString::fromUtf8("? Диагностировать сейчас"), this);
    m_runBtn_->setObjectName("boostButton");
    m_runBtn_->setFixedHeight(36);
    controlRow->addWidget(m_runBtn_);
    controlRow->addStretch();
    mainLayout->addLayout(controlRow);

    m_adminLabel_ = new QLabel(this);
    m_adminLabel_->setObjectName("sectionSubtitle");
    bool admin = SystemAudit::isAdmin();
    m_adminLabel_->setText(admin
        ? QString::fromUtf8("Права администратора: есть — все оптимизации доступны.")
        : QString::fromUtf8("Права администратора: нет — часть оптимизаций (MTU, DNS, план питания) будет недоступна. "
                            "Запустите программу «от имени администратора»."));
    m_adminLabel_->setStyleSheet(admin
        ? "color:#22c55e; font-size:12px; background:transparent;"
        : "color:#f59e0b; font-size:12px; background:transparent;");
    m_adminLabel_->setWordWrap(true);
    mainLayout->addWidget(m_adminLabel_);

    m_runningLabel_ = new QLabel(QString::fromUtf8("Диагностика не запускалась."), this);
    m_runningLabel_->setObjectName("sectionSubtitle");
    mainLayout->addWidget(m_runningLabel_);

    m_summaryLabel_ = new QLabel(this);
    m_summaryLabel_->setObjectName("gameTitle");
    mainLayout->addWidget(m_summaryLabel_);

    auto* resultsScroll = new QScrollArea(this);
    resultsScroll->setWidgetResizable(true);
    resultsScroll->setFrameShape(QFrame::NoFrame);
    m_resultsList_ = new QWidget();
    m_resultsList_->setLayout(new QVBoxLayout(m_resultsList_));
    m_resultsList_->layout()->setContentsMargins(0, 0, 0, 0);
    m_resultsList_->layout()->setSpacing(6);
    resultsScroll->setWidget(m_resultsList_);
    mainLayout->addWidget(resultsScroll, 1);

    auto* problemsTitle = new QLabel(QString::fromUtf8("Частые проблемы и решения"), this);
    problemsTitle->setObjectName("sectionTitle");
    mainLayout->addWidget(problemsTitle);

    m_problemSearch_ = new QLineEdit(this);
    m_problemSearch_->setObjectName("searchBox");
    m_problemSearch_->setPlaceholderText(QString::fromUtf8("\xD0\x9F\xD0\xBE\xD0\xB8\xD1\x81\xD0\xBA \xD0\xBF\xD0\xBE \xD0\xBF\xD1\x80\xD0\xBE\xD0\xB1\xD0\xBB\xD0\xB5\xD0\xBC\xD0\xB0\xD0\xBC..."));
    mainLayout->addWidget(m_problemSearch_);


    auto* problemsScroll = new QScrollArea(this);
    problemsScroll->setWidgetResizable(true);
    problemsScroll->setFrameShape(QFrame::NoFrame);
    m_problemsList_ = new QWidget();
    m_problemsList_->setLayout(new QVBoxLayout(m_problemsList_));
    m_problemsList_->layout()->setContentsMargins(0, 0, 0, 0);
    m_problemsList_->layout()->setSpacing(6);
    problemsScroll->setWidget(m_problemsList_);
    mainLayout->addWidget(problemsScroll);

    auto* capsTitle = new QLabel(QString::fromUtf8("Что программа может и не может сделать"), this);
    capsTitle->setObjectName("sectionTitle");
    mainLayout->addWidget(capsTitle);

    auto* capsScroll = new QScrollArea(this);
    capsScroll->setWidgetResizable(true);
    capsScroll->setFrameShape(QFrame::NoFrame);
    m_capabilitiesList_ = new QWidget();
    m_capabilitiesList_->setLayout(new QVBoxLayout(m_capabilitiesList_));
    m_capabilitiesList_->layout()->setContentsMargins(0, 0, 0, 0);
    m_capabilitiesList_->layout()->setSpacing(6);
    capsScroll->setWidget(m_capabilitiesList_);
    mainLayout->addWidget(capsScroll);

    auto* plainBtn = new QPushButton(QString::fromUtf8("\xD0\x9E\xD0\xB1\xD1\x8A\xD1\x8F\xD0\xB2\xD0\xBD\xD0\xB8\xD1\x82\xD1\x8C \xD0\xBF\xD1\x80\xD0\xBE\xD1\x81\xD1\x82\xD1\x8B\xD0\xBC\xD0\xB8 \xD1\x81\xD0\xBB\xD0\xBE\xD0\xB2\xD0\xB0\xD0\xBC\xD0\xB8"), this);
    plainBtn->setObjectName("boostButton");
    plainBtn->setFixedHeight(34);
    m_plainText_ = new QTextEdit(this);
    m_plainText_->setReadOnly(true);
    m_plainText_->setMaximumHeight(230);
    m_plainText_->setVisible(false);
    mainLayout->addWidget(plainBtn);
    mainLayout->addWidget(m_plainText_);

    auto* exportBtn = new QPushButton(QString::fromUtf8(
        "\xD0\xAD\xD0\xBA\xD1\x81\xD0\xBF\xD0\xBE\xD1%80\xD1\x82\x20\xD0\xBE\xD1\x82\xD1%87\xD0%B5\xD1\x82\xD0\xB0\x20\x28JSON\x29"), this);
    exportBtn->setObjectName(QStringLiteral("boostButton"));
    exportBtn->setFixedHeight(34);
    mainLayout->addWidget(exportBtn);
    connect(exportBtn, &QPushButton::clicked, this, [this]() {
        std::vector<gno::ReportExporter::Check> checks;
        if (m_hasDiag_)
            for (const auto& c : m_lastDiag_.checks)
                checks.push_back({c.name, c.severity});
        auto& svc = MonitoringService::instance();
        const int score = static_cast<int>(
            ConnectionGrader::evaluate(svc.currentPing(), svc.currentJitter(),
                                       svc.currentLossPercent()).score);
        const std::string json = ReportExporter::build(
            (m_lastGameName.isEmpty() ? QStringLiteral("unknown") : m_lastGameName).toStdString(),
            svc.currentPing(), svc.currentJitter(), svc.currentLossPercent(), score,
            {}, -1, checks);
        const QString dir = qEnvironmentVariable("APPDATA") + QStringLiteral("/GNO/Reports");

        QDir().mkpath(dir);
        const QString path = dir + QStringLiteral("/report_%1.json")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
        gno::persistence::atomicWriteText(path.toStdString(), json);
        m_runningLabel_->setText(QString::fromUtf8(
            "\xD0\x9E\xD1\x82\xD1%87\xD1%91\xD1%82\x3A\x20") + path);
    });

    connect(plainBtn, &QPushButton::clicked, this, &DiagnosticsWidget::onPlainLanguageClicked);
    connect(m_problemSearch_, &QLineEdit::textChanged, this, &DiagnosticsWidget::onSearchChanged);

    connect(m_runBtn_, &QPushButton::clicked, this, [this]() {
        runDiagnostics(m_gameCombo_->currentText(),
                       m_gameCombo_->currentData().toString());
    });
    connect(m_gameCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DiagnosticsWidget::onGameSelected);
}

void DiagnosticsWidget::onGameSelected(int index)
{
    QString game = m_gameCombo_->itemText(index);
    if (game.isEmpty() || game.startsWith(QString::fromUtf8("—")))
        game = QString();
    renderProblems(game);
}

QWidget* DiagnosticsWidget::makeCheckCard(const DiagnosticCheck& c)
{
    auto* card = new QWidget(this);
    card->setObjectName(c.passed ? "gameCard" : "settingsGroup");
    card->setStyleSheet(QString("QWidget#gameCard, QWidget#settingsGroup { border-left: 3px solid %1; }")
                            .arg(statusColor(c.severity)));

    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 10, 14, 10);
    layout->setSpacing(4);

    auto* head = new QHBoxLayout();
    auto* iconLbl = new QLabel(statusIcon(c.severity), card);
    iconLbl->setStyleSheet(QString("color:%1; font-size:16px; font-weight:700; background:transparent;")
                               .arg(statusColor(c.severity)));
    head->addWidget(iconLbl);

    auto* nameLbl = new QLabel(QString::fromStdString(c.name), card);
    nameLbl->setObjectName("gameTitle");
    head->addWidget(nameLbl);

    auto* catLbl = new QLabel(QString::fromStdString(c.category), card);
    catLbl->setObjectName("gameCategory");
    head->addWidget(catLbl);
    head->addStretch();
    layout->addLayout(head);

    auto* detailLbl = new QLabel(QString::fromStdString(c.detail), card);
    detailLbl->setObjectName("sectionSubtitle");
    detailLbl->setWordWrap(true);
    layout->addWidget(detailLbl);

    auto* explainLbl = new QLabel(QString::fromUtf8("Как программа это видит: ") +
                                      QString::fromStdString(c.explanation), card);
    explainLbl->setStyleSheet("color:rgba(255,255,255,0.45); font-size:11px; font-style:italic; background:transparent;");
    explainLbl->setWordWrap(true);
    layout->addWidget(explainLbl);

    if (!c.recommendation.empty()) {
        auto* recLbl = new QLabel(QString::fromUtf8("Что сделать: ") +
                                      QString::fromStdString(c.recommendation), card);
        recLbl->setStyleSheet(QString("color:%1; font-size:12px; background:transparent;")
                                  .arg(c.severity ? theme::Colors::WARNING : theme::Colors::SUCCESS));
        recLbl->setWordWrap(true);
        layout->addWidget(recLbl);
    }

    if (!c.fix_action.empty()) {
        auto* fixBtn = new QPushButton(QString::fromUtf8("Исправить автоматически"), card);
        fixBtn->setObjectName("boostButton");
        fixBtn->setFixedWidth(190);
        fixBtn->setFixedHeight(30);
        QString action = QString::fromStdString(c.fix_action);
        connect(fixBtn, &QPushButton::clicked, this, [this, action]() {
            QString result = QString::fromStdString(LaunchDiagnostics::applyFix(action.toStdString()));
            m_runningLabel_->setText(QString::fromUtf8("? %1").arg(result));
            renderCapabilities();
        });
        layout->addWidget(fixBtn, 0, Qt::AlignLeft);
    }

    return card;
}

QWidget* DiagnosticsWidget::makeProblemCard(const ProblemEntry& e)
{
    auto* card = new QWidget(this);
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

    auto addRow = [&](const QString& prefix, const QString& text, const QString& color) {
        auto* lbl = new QLabel(QString::fromUtf8("%1: %2").arg(prefix, text), card);
        lbl->setStyleSheet(QString("color:%1; font-size:12px; background:transparent;").arg(color));
        lbl->setWordWrap(true);
        layout->addWidget(lbl);
    };

    addRow(QString::fromUtf8("Симптомы"), QString::fromStdString(e.symptoms), "rgba(255,255,255,0.7)");
    addRow(QString::fromUtf8("Причина"), QString::fromStdString(e.cause), "rgba(255,255,255,0.5)");
    addRow(QString::fromUtf8("Решение через программу"), QString::fromStdString(e.solution), theme::Colors::ACCENT_CYAN);

    if (!e.fix_action.empty()) {
        auto* fixBtn = new QPushButton(QString::fromUtf8("Применить решение"), card);
        fixBtn->setObjectName("boostButton");
        fixBtn->setFixedWidth(180);
        fixBtn->setFixedHeight(30);
        QString action = QString::fromStdString(e.fix_action);
        connect(fixBtn, &QPushButton::clicked, this, [this, e]() {
            QString result = QString::fromStdString(ProblemDb::applyAutoFix(e));
            m_runningLabel_->setText(QString::fromUtf8("? %1").arg(result));
            renderCapabilities();
        });
        layout->addWidget(fixBtn, 0, Qt::AlignLeft);
    }

    return card;
}

void DiagnosticsWidget::renderResults(const GameDiagnostics& diag)
{
    auto* layout = qobject_cast<QVBoxLayout*>(m_resultsList_->layout());
    if (!layout) return;

    QLayoutItem* item;
    while ((item = layout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    m_summaryLabel_->setText(
        QString::fromUtf8("Диагностика «%1»: %2 проверок · %3 ок · %4 предупреждений · %5 ошибок")
            .arg(diag.game_name.empty() ? QString::fromUtf8("система") : QString::fromStdString(diag.game_name))
            .arg(diag.checks.size())
            .arg(diag.passed_count)
            .arg(diag.warning_count)
            .arg(diag.error_count));

    for (const auto& c : diag.checks)
        layout->addWidget(makeCheckCard(c));

    layout->addStretch();
}

void DiagnosticsWidget::renderProblems(const QString& gameName)
{
    m_lastGameName = gameName;
    m_lastGameName = gameName;
    auto* layout = qobject_cast<QVBoxLayout*>(m_problemsList_->layout());
    if (!layout) return;

    QLayoutItem* item;
    while ((item = layout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    if (gameName.isEmpty()) {
        auto* hint = new QLabel(QString::fromUtf8("Выберите игру — программа покажет частые проблемы и способы их решения через неё."), this);
        hint->setObjectName("sectionSubtitle");
        hint->setWordWrap(true);
        layout->addWidget(hint);
        layout->addStretch();
        return;
    }

    auto problems = ProblemDb::search(gameName.toStdString(), m_problemQuery_.toStdString());
    if (problems.empty()) {
        auto* hint = new QLabel(QString::fromUtf8("Для этой игры пока нет записей в базе решений."), this);
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

    QLayoutItem* item;
    while ((item = layout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    auto caps = SystemAudit::getCapabilities();
    for (const auto& c : caps) {
        auto* card = new QWidget(this);
        card->setObjectName("gameCard");

        auto* row = new QHBoxLayout(card);
        row->setContentsMargins(12, 8, 12, 8);
        row->setSpacing(12);

        QColor statusColor = c.currently_possible ? QColor(theme::Colors::SUCCESS)
                             : c.requires_vpn_server ? QColor(theme::Colors::TEXT_TERTIARY)
                                                     : QColor(theme::Colors::WARNING);
        auto* iconLbl = new QLabel(c.currently_possible ? QString::fromUtf8("?")
                                : c.requires_vpn_server ? QString::fromUtf8("?")
                                                        : QString::fromUtf8("?"), card);
        iconLbl->setStyleSheet(QString("color:%1; font-size:15px; font-weight:700; background:transparent;")
                                   .arg(statusColor.name()));
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
                                     .arg(statusColor.name()));
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

    m_runBtn_->setEnabled(false);
    m_runningLabel_->setText(QString::fromUtf8("Запускаем диагностику… проверяем сеть, систему и игру."));

    std::string game = gameName.toStdString();
    std::string proc = processName.toStdString();
    std::thread([this, game, proc]() {
        GameDiagnostics diag = LaunchDiagnostics::run(game, proc);

        // v2.1: region advisor - probe one node per region, pick the best.
        QString region_advice;
        {
            SpeedTest st;
            struct R { const char* name; const char* ip; } regions[] = {
                {"EU", "1.1.1.1"}, {"NA", "104.16.132.229"}, {"ASIA", "103.2.131.17"}};
            double best = 1e9; const char* best_name = nullptr; int best_ms = -1;
            for (auto& r : regions) {
                auto res = st.benchmarkServer(r.ip, 3);
                if (!res.success) continue;
                if (res.latency_ms < best) { best = res.latency_ms; best_name = r.name; best_ms = static_cast<int>(res.latency_ms); }
            }
            if (best_name)
                region_advice = QString::fromUtf8(
                    "\xD0\xA0\xD0\xB5\xD0\xBA\xD0\xBE%D0\xBC\xD0%B5%D0\xBD\xD0%B4\xD1%83\xD0%B5\xD0\xBC\xD1%8B\xD0\xB9\x20\xD1%80\xD0%B5%D0%B3\xD0%B8%D0\xBE\xD0\xBD\x3A\x20") +
                    best_name + QStringLiteral(" (%1 ms)").arg(best_ms);
        }

        QMetaObject::invokeMethod(this, [this, diag, region_advice]() {
            m_running_.store(false);
            m_runBtn_->setEnabled(true);
            m_lastDiag_ = diag;
            m_hasDiag_ = true;
            if (!region_advice.isEmpty())
                m_runningLabel_->setText(m_runningLabel_->text() + "  " + region_advice);
            renderResults(diag);
            m_runningLabel_->setText(QString::fromUtf8(
                "Готово. Диагностика выполнена в %1.").arg(QDateTime::currentDateTime().toString("HH:mm:ss")));
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
        out += QString::fromUtf8("=== ") + QString::fromStdString(s.title) + QString::fromUtf8(" ===") + "\n";
        for (const auto& l : s.lines)
            out += QString::fromUtf8("- ") + QString::fromStdString(l) + "\n";
        out += "\n";
    }
    m_plainText_->setPlainText(out);
    m_plainText_->setVisible(true);
}
} // namespace gno