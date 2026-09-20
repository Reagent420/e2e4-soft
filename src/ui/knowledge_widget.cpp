#include "knowledge_widget.h"
#include "core/problem_db.h"
#include "monitoring_service.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QTextBrowser>

namespace gno {

KnowledgeWidget::KnowledgeWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(10);

    auto* title = new QLabel(QString::fromUtf8("База знаний — известные проблемы и решения"), this);
    title->setStyleSheet("font-size: 18px; font-weight: 600; background: transparent;");
    root->addWidget(title);

    // Поиск + фильтр по игре + подбор под текущий диагноз
    auto* filterRow = new QHBoxLayout();
    auto* search = new QLineEdit(this);
    search->setPlaceholderText(QString::fromUtf8("Поиск по симптомам, причинам, решениям..."));
    search->setClearButtonEnabled(true);
    auto* gameFilter = new QComboBox(this);
    gameFilter->addItem(QString::fromUtf8("Все игры"), QString());
    for (const auto& g : ProblemDb::getKnownGames())
        gameFilter->addItem(QString::fromStdString(g), QString::fromStdString(g));
    gameFilter->setMinimumWidth(160);

    // Связь с диагностикой: раньше база знаний была оторвана от неё — человек
    // видел «проблема в локальной сети» и сам искал статью среди сотни.
    auto* diagBtn = new QPushButton(QString::fromUtf8("По последнему диагнозу"), this);
    diagBtn->setObjectName("sidebarButton");
    diagBtn->setCursor(Qt::PointingHandCursor);

    filterRow->addWidget(search, 1);
    filterRow->addWidget(gameFilter);
    filterRow->addWidget(diagBtn);
    root->addLayout(filterRow);

    auto* splitter = new QSplitter(Qt::Horizontal, this);

    m_list = new QListWidget(splitter);
    m_list->setMaximumWidth(340);

    auto* detailWrap = new QWidget(splitter);
    auto* detailLayout = new QVBoxLayout(detailWrap);
    detailLayout->setContentsMargins(12, 0, 0, 0);
    m_detail = new QTextBrowser(detailWrap);
    m_detail->setOpenExternalLinks(false);

    m_applyBtn = new QPushButton(QString::fromUtf8("Применить связанный фикс"), detailWrap);
    m_applyBtn->setObjectName("sidebarButton");
    m_applyBtn->setCursor(Qt::PointingHandCursor);
    m_applyBtn->setMaximumWidth(260);
    m_applyBtn->hide();

    detailLayout->addWidget(m_detail, 1);
    detailLayout->addWidget(m_applyBtn, 0, Qt::AlignLeft);
    splitter->addWidget(m_list);
    splitter->addWidget(detailWrap);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    root->addWidget(splitter, 1);

    auto reload = [this, search, gameFilter]() {
        const std::string game = gameFilter->currentData().toString().toStdString();
        const std::string q = search->text().toStdString();
        if (q.empty())
            showEntries(game.empty() ? ProblemDb::getAll() : ProblemDb::getForGame(game));
        else
            showEntries(ProblemDb::search(game, q));
    };

    connect(search, &QLineEdit::textChanged, this, [reload]() { reload(); });
    connect(gameFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [reload](int) { reload(); });
    connect(diagBtn, &QPushButton::clicked, this, &KnowledgeWidget::showDiagnosticMatches);

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0 || row >= static_cast<int>(m_entries.size())) {
            m_applyBtn->hide();
            return;
        }
        const auto& e = m_entries[static_cast<std::size_t>(row)];
        QString html = "<h3>" + QString::fromStdString(e.title) + "</h3>"
            + "<p><b>" + QString::fromUtf8("Игра") + ":</b> " + QString::fromStdString(e.game) + "<br>"
            + "<b>" + QString::fromUtf8("Сложность") + ":</b> " + QString::fromStdString(e.difficulty) + "</p>"
            + "<p><b>" + QString::fromUtf8("Симптомы") + ":</b> " + QString::fromStdString(e.symptoms) + "</p>"
            + "<p><b>" + QString::fromUtf8("Причина") + ":</b> " + QString::fromStdString(e.cause) + "</p>"
            + "<p><b>" + QString::fromUtf8("Решение") + ":</b> " + QString::fromStdString(e.solution) + "</p>";
        if (e.fix_action.empty())
            html += "<p style='opacity:0.7'>" + QString::fromUtf8("(только ручное решение — автофикса нет)") + "</p>";
        m_detail->setHtml(html);
        m_applyBtn->setVisible(!e.fix_action.empty());
        m_currentFix = QString::fromStdString(e.fix_action);
    });

    connect(m_applyBtn, &QPushButton::clicked, this, [this]() {
        if (!m_currentFix.isEmpty())
            emit applyRequested(m_currentFix);
    });

    reload();
}

void KnowledgeWidget::showEntries(const std::vector<ProblemEntry>& entries) {
    m_list->clear();
    m_entries = entries;
    for (std::size_t i = 0; i < m_entries.size(); ++i) {
        const auto& e = m_entries[i];
        auto* it = new QListWidgetItem(
            QString::fromStdString(e.game) + " — " + QString::fromStdString(e.title), m_list);
        it->setData(Qt::UserRole, static_cast<int>(i));
    }
    m_detail->clear();
    m_applyBtn->hide();
    m_currentFix.clear();
}

void KnowledgeWidget::showDiagnosticMatches() {
    const MonitoringService& svc = MonitoringService::instance();
    if (!svc.hasDiagnosticReport()) {
        m_list->clear();
        m_entries.clear();
        m_applyBtn->hide();
        m_detail->setHtml(QString::fromUtf8(
            "<h3>Пока нечего подбирать</h3>"
            "<p>Диагностика маршрута ещё не выполнялась. Откройте «Диагностика» → "
            "вкладка «Маршрут до сервера» и запустите проверку — после этого здесь "
            "появятся статьи под вашу ситуацию.</p>"));
        return;
    }

    const auto matches = ProblemDb::forDiagnostic(svc.lastDiagnosticReport());
    if (matches.empty()) {
        m_list->clear();
        m_entries.clear();
        m_applyBtn->hide();
        m_detail->setHtml(QString::fromUtf8(
            "<h3>Подходящих статей нет</h3>"
            "<p>По последнему диагнозу ничего конкретного не нашлось — "
            "воспользуйтесь поиском выше.</p>"));
        return;
    }

    showEntries(matches);
    m_detail->setHtml(QString::fromUtf8("<h3>Подбор по последнему диагнозу</h3><p>Найдено статей: %1</p>")
                          .arg(matches.size()));
}

} // namespace gno
