#include "session_history_widget.h"
#include "theme.h"

#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QtCharts/QChartView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMessageBox>
#include <QScrollArea>
#include <QFrame>

#include "../core/session_history.h"
#include "../core/report_exporter.h"
#include "report_export.h"

namespace gno {

SessionHistoryWidget::SessionHistoryWidget(QWidget* parent)
    : QWidget(parent), m_history(new SessionHistory()) {
    setupUI();
    refreshHistory();
}

void SessionHistoryWidget::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(14);

    auto* title = new QLabel("История сессий", this);
    title->setObjectName("sectionTitle");
    mainLayout->addWidget(title);

    auto* subtitle = new QLabel("Хронология замеров качества сети", this);
    subtitle->setObjectName("sectionSubtitle");
    mainLayout->addWidget(subtitle);

    m_statsLabel = new QLabel(this);
    m_statsLabel->setObjectName("gameTitle");
    mainLayout->addWidget(m_statsLabel);

    // Buttons row
    auto* btnRow = new QHBoxLayout();

    auto* refreshBtn = new QPushButton("Обновить", this);
    refreshBtn->setObjectName("boostButton");
    refreshBtn->setFixedWidth(120);
    connect(refreshBtn, &QPushButton::clicked, this,
            &SessionHistoryWidget::refreshHistory);
    btnRow->addWidget(refreshBtn);

    auto* exportPngBtn = new QPushButton("Экспорт PNG", this);
    exportPngBtn->setObjectName("boostButton");
    exportPngBtn->setFixedWidth(140);
    connect(exportPngBtn, &QPushButton::clicked, this, [this]() {
        auto records = m_history->getAll();
        QString path = gno::report::defaultReportsDir() + "/history_report.png";
        bool ok = gno::report::exportHistoryReport(path, records,
                    m_history->getAveragePing(), m_history->getAverageJitter());
        QMessageBox::information(this, "Экспорт отчёта",
            ok ? QString("Отчёт сохранён:\n%1").arg(path)
               : QString("Не удалось сохранить отчёт."));
    });
    btnRow->addWidget(exportPngBtn);

    auto* csvBtn = new QPushButton("Экспорт CSV", this);
    csvBtn->setObjectName("boostButton");
    csvBtn->setFixedWidth(130);
    connect(csvBtn, &QPushButton::clicked, this, [this]() {
        const QString path =
            QString::fromStdString(m_history->getSavePath())
            .section('.', 0, -2) + QStringLiteral(".csv");
        const bool ok = m_history->exportCsv(path.toStdString());
        QMessageBox::information(this, QStringLiteral("CSV"),
            ok ? path : QStringLiteral("Export failed"));
    });
    btnRow->addWidget(csvBtn);

    auto* clearBtn = new QPushButton("Очистить", this);
    clearBtn->setObjectName("sidebarButton");
    clearBtn->setFixedWidth(120);
    connect(clearBtn, &QPushButton::clicked, this, [this]() {
        m_history->clear();
        refreshHistory();
    });
    btnRow->addWidget(clearBtn);
    btnRow->addStretch();
    mainLayout->addLayout(btnRow);

    // Score trend chart
    m_chart = new QChartView(this);
    m_chart->setRenderHint(QPainter::Antialiasing);
    m_chart->setMinimumHeight(220);
    m_chart->chart()->setTheme(QChart::ChartThemeDark);
    m_chart->chart()->legend()->hide();
    m_chart->setStyleSheet(QString(
        "QChartView { border: 1px solid %1; border-radius: 8px;"
        " background-color: %2; }")
            .arg(theme::Colors::BORDER, theme::Colors::BG_SURFACE));
    mainLayout->addWidget(m_chart);

    // Session records scroll area
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    m_sessionList = new QWidget();
    m_sessionList->setLayout(new QVBoxLayout(m_sessionList));
    m_sessionList->layout()->setContentsMargins(0, 0, 0, 0);
    m_sessionList->layout()->setSpacing(4);
    scrollArea->setWidget(m_sessionList);
    mainLayout->addWidget(scrollArea, 1);
}

void SessionHistoryWidget::refreshHistory() {
    auto records = m_history->getAll();

    // Stats summary
    m_statsLabel->setText(QString(
        "%1 сессий · Средний пинг: %2 ms · Оценка: %3")
        .arg(records.size())
        .arg(m_history->getAveragePing(), 0, 'f', 1)
        .arg(ConnectionGrader::evaluate(
            m_history->getAveragePing(),
            m_history->getAverageJitter(),
            0).score));

    // Score trend chart
    auto* series = new QLineSeries();
    series->setColor(QColor(theme::Colors::ACCENT_NEON));
    int i = 0;
    bool has_data = false;
    for (const auto& r : records) {
        series->append(i, r.quality_score);
        if (r.quality_score > 0) has_data = true;
        ++i;
    }
    auto* chart = new QChart();
    chart->addSeries(series);
    auto* ax = new QValueAxis();
    auto* ay = new QValueAxis();
    ax->setRange(0, std::max(1, i - 1));
    ay->setRange(0, 100);
    chart->addAxis(ax, Qt::AlignBottom); series->attachAxis(ax);
    chart->addAxis(ay, Qt::AlignLeft); series->attachAxis(ay);
    auto* old_chart = m_chart->chart();
    m_chart->setChart(chart);
    delete old_chart;

    // Session cards
    auto* layout = qobject_cast<QVBoxLayout*>(m_sessionList->layout());
    if (!layout) return;
    QLayoutItem* item;
    while ((item = layout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    if (records.empty()) {
        auto* hint = new QLabel("Замеров пока не выполнялось.", m_sessionList);
        hint->setObjectName("sectionSubtitle");
        layout->addWidget(hint);
        layout->addStretch();
        return;
    }

    // Show last 20 sessions as cards
    int shown = 0;
    for (auto it = records.rbegin(); it != records.rend() && shown < 20; ++it, ++shown) {
        auto* card = new QWidget(m_sessionList);
        card->setObjectName("gameCard");
        auto* cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(12, 8, 12, 8);

        auto* nameLbl = new QLabel(
            QString("%1\n%2 → %3")
                .arg(QString::fromStdString(it->game_name),
                     QString::fromStdString(it->start_time_str),
                     QString::fromStdString(it->end_time_str)),
            card);
        nameLbl->setWordWrap(true);
        cardLayout->addWidget(nameLbl, 1);

        auto* scoreLbl = new QLabel(
            QString::number(static_cast<int>(it->quality_score)), card);
        scoreLbl->setStyleSheet(QString(
            "font-size:20px; font-weight:bold; color:%1;")
            .arg(it->quality_score > 70 ? theme::Colors::SUCCESS
                 : it->quality_score > 40 ? theme::Colors::WARNING
                 : theme::Colors::ERROR));
        cardLayout->addWidget(scoreLbl);

        layout->addWidget(card);
    }
    layout->addStretch();
}

} // namespace gno
