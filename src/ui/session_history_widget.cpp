#include "session_history_widget.h"
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QtCharts/QChartView>
#include <QVBoxLayout>
#include <algorithm>
#include "theme.h"
#include "report_export.h"

#include <QVBoxLayout>
#include <algorithm>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QFrame>
#include <QMessageBox>

#include "../core/session_history.h"

namespace gno {

SessionHistoryWidget::SessionHistoryWidget(QWidget* parent)
    : QWidget(parent)
{
    m_history = new SessionHistory();
    setupUI();
    refreshHistory();
}

void SessionHistoryWidget::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(16);

    auto* title = new QLabel(QString::fromUtf8("История сессий"), this);
    title->setObjectName("sectionTitle");
    mainLayout->addWidget(title);

    auto* subtitle = new QLabel(QString::fromUtf8("Показатели сети за прошлые игровые сессии"), this);
    subtitle->setObjectName("sectionSubtitle");
    mainLayout->addWidget(subtitle);

    m_statsLabel = new QLabel(this);
    m_statsLabel->setObjectName("gameTitle");
    mainLayout->addWidget(m_statsLabel);

    auto* btnRow = new QHBoxLayout();
    auto* refreshBtn = new QPushButton(QString::fromUtf8("Обновить"), this);
    refreshBtn->setObjectName("boostButton");
    refreshBtn->setFixedWidth(120);
    connect(refreshBtn, &QPushButton::clicked, this, &SessionHistoryWidget::refreshHistory);
    btnRow->addWidget(refreshBtn);

    auto* exportBtn = new QPushButton(QString::fromUtf8("Экспорт отчёта PNG"), this);
    exportBtn->setObjectName("boostButton");
    exportBtn->setFixedWidth(160);
connect(exportBtn, &QPushButton::clicked, this, [this]() {
        auto records = m_history->getAll();
        QString path = gno::report::defaultReportsDir() + "/history_report.png";
        bool ok = gno::report::exportHistoryReport(path, records,
                    m_history->getAveragePing(), m_history->getAverageJitter());
        QMessageBox::information(this, QString::fromUtf8("Экспорт отчёта"),
            ok ? QString::fromUtf8("Отчёт сохранён:\n%1").arg(path)
               : QString::fromUtf8("Не удалось сохранить отчёт."));
    });
    btnRow->addWidget(exportBtn);
    auto* csvBtn = new QPushButton(QString::fromUtf8("\xD0\xAD\xD0\xBA\xD1\x81\xD0\xBF\xD0\xBE\xD1\x80\xD1\x82\x20CSV"), this);
    csvBtn->setObjectName("boostButton");
    csvBtn->setFixedWidth(140);
    connect(csvBtn, &QPushButton::clicked, this, [this]() {
        const QString path = QString::fromStdString(m_history->getSavePath());

        const bool ok = m_history->exportCsv(path.toStdString());
        QMessageBox::information(this, QStringLiteral("CSV"), ok ? path : QStringLiteral("Export failed"));
    });
    btnRow->addWidget(csvBtn);

    auto* clearBtn = new QPushButton(QString::fromUtf8("Очистить историю"), this);
    clearBtn->setObjectName("sidebarButton");
    clearBtn->setFixedWidth(120);
    connect(clearBtn, &QPushButton::clicked, this, [this]() {
        m_history->clear();
        refreshHistory();
    });
    btnRow->addWidget(clearBtn);
    btnRow->addStretch();
    mainLayout->addLayout(btnRow);
    // v2.1: quality score trend chart
    m_chart = new QChartView(this);
    m_chart->setRenderHint(QPainter::Antialiasing);
    m_chart->setMinimumHeight(220);
    m_chart->chart()->setTheme(QChart::ChartThemeDark);
    m_chart->chart()->legend()->hide();

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    m_sessionList = new QWidget();
    m_sessionList->setLayout(new QVBoxLayout(m_sessionList));
    m_sessionList->layout()->setContentsMargins(0, 0, 0, 0);
    m_sessionList->layout()->setSpacing(4);
    scrollArea->setWidget(m_sessionList);

    mainLayout->addWidget(scrollArea);
}

void SessionHistoryWidget::refreshHistory()
{
    auto records = m_history->getAll();

    double avgPing = m_history->getAveragePing();
    double avgJitter = m_history->getAverageJitter();
    m_statsLabel->setText(
        QString("Сессий: %1 | Средний пинг: %2 мс | Средний джиттер: %3 мс")
            .arg(records.size())
            .arg(avgPing, 0, 'f', 1)
            .arg(avgJitter, 0, 'f', 1));

    auto* layout = qobject_cast<QVBoxLayout*>(m_sessionList->layout());
    if (!layout) return;

    QLayoutItem* item;
    while ((item = layout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    for (int i = static_cast<int>(records.size()) - 1; i >= 0 && i >= static_cast<int>(records.size()) - 50; --i) {
        const auto& r = records[i];

        auto* card = new QWidget(m_sessionList);
        card->setObjectName("gameCard");
        card->setFixedHeight(56);

        auto* cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(12, 8, 12, 8);
        cardLayout->setSpacing(16);

        auto* gameLbl = new QLabel(QString::fromStdString(r.game_name), card);
        gameLbl->setObjectName("gameTitle");
        gameLbl->setFixedWidth(160);
        cardLayout->addWidget(gameLbl);

        auto* timeLbl = new QLabel(QString::fromStdString(r.start_time_str), card);
        timeLbl->setObjectName("sectionSubtitle");
        timeLbl->setFixedWidth(140);
        cardLayout->addWidget(timeLbl);

        auto* pingLbl = new QLabel(QString("%1 мс").arg(r.avg_ping_ms, 0, 'f', 1), card);
        pingLbl->setObjectName("sectionSubtitle");
        pingLbl->setFixedWidth(80);
        cardLayout->addWidget(pingLbl);

        auto* jitterLbl = new QLabel(QString("Д: %1 мс").arg(r.avg_jitter_ms, 0, 'f', 1), card);
        jitterLbl->setObjectName("sectionSubtitle");
        jitterLbl->setFixedWidth(80);
        cardLayout->addWidget(jitterLbl);

        auto* lossLbl = new QLabel(QString("Потери: %1%").arg(r.avg_packet_loss, 0, 'f', 1), card);
        lossLbl->setObjectName("sectionSubtitle");
        lossLbl->setFixedWidth(80);
        cardLayout->addWidget(lossLbl);

        if (r.boost_was_active) {
            auto* boostTag = new QLabel(QString::fromUtf8("УСКОРЕНИЕ"), card);
            boostTag->setObjectName("gameCategory");
            cardLayout->addWidget(boostTag);
        }

        cardLayout->addStretch();
        layout->addWidget(card);
    }

    layout->addStretch();

    // v2.1: quality score trend
    {
        auto records = m_history->getLast(30);
        auto* series = new QLineSeries();
        series->setColor(QColor(theme::Colors::ACCENT_NEON));
        int i = 0;
        for (const auto& r : records) {
            series->append(i, r.quality_score);
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
        auto* old = m_chart->chart();
        m_chart->setChart(chart);
        delete old;
    }
}

} // namespace gnoo
