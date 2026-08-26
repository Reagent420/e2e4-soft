#include "dashboard.h"
#include "theme.h"
#include "monitoring_service.h"
#include "report_export.h"
#include "core/fps_boost.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QStyle>
#include <QFont>
#include <QFileDialog>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>

namespace gno {

// ---------------------------------------------------------------------------
// PingGraphWidget
// ---------------------------------------------------------------------------

PingGraphWidget::PingGraphWidget(QWidget* parent)
    : QWidget(parent) {
    setObjectName("metricCard");
    setFixedHeight(180);
    for (int i = 0; i < kMaxPoints; ++i)
        data_.append(0.0);
}

void PingGraphWidget::addPingValue(double ms) {
    data_.append(ms);
    if (data_.size() > kMaxPoints)
        data_.removeFirst();
    update();
}

void PingGraphWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QRect area = rect().adjusted(12, 8, -12, -8);
    if (area.width() <= 0 || area.height() <= 0)
        return;

    // grid
    p.setPen(QPen(QColor(255, 255, 255, 12), 1, Qt::DashLine));
    for (int v : {0, 25, 50, 75, 100}) {
        int y = area.bottom() - static_cast<int>(v / 100.0 * area.height());
        p.drawLine(area.left(), y, area.right(), y);
    }

    // y-axis labels
    p.setPen(QColor(255, 255, 255, 80));
    QFont small = p.font();
    small.setPixelSize(10);
    p.setFont(small);
    for (int v : {0, 25, 50, 75, 100}) {
        int y = area.bottom() - static_cast<int>(v / 100.0 * area.height());
        p.drawText(area.left() - 2, y + 3, QString::number(v));
    }

    int count = data_.size();
    if (count < 2)
        return;

    double step = static_cast<double>(area.width()) / (kMaxPoints - 1);

    // build line path
    QPainterPath linePath;
    for (int i = 0; i < count; ++i) {
        double x = area.left() + (kMaxPoints - count + i) * step;
        double y = area.bottom() - (qBound(0.0, data_[i], 100.0) / 100.0) * area.height();
        if (i == 0)
            linePath.moveTo(x, y);
        else
            linePath.lineTo(x, y);
    }

    // gradient fill
    QPainterPath fillPath(linePath);
    double lastX = area.left() + (kMaxPoints - 1) * step;
    fillPath.lineTo(lastX, area.bottom());
    fillPath.lineTo(area.left() + (kMaxPoints - count) * step, area.bottom());
    fillPath.closeSubpath();

    QLinearGradient grad(0, area.top(), 0, area.bottom());
    grad.setColorAt(0.0, QColor(59, 130, 246, 80));
    grad.setColorAt(1.0, QColor(59, 130, 246, 0));
    p.fillPath(fillPath, grad);

    // line
    p.setPen(QPen(QColor(0x3B, 0x82, 0xF6), 2));
    p.drawPath(linePath);
}

// ---------------------------------------------------------------------------
// DashboardWidget helpers
// ---------------------------------------------------------------------------

static QLabel* makeSmallLabel(const QString& text, const QColor& color = QColor(255, 255, 255, 100)) {
    auto* l = new QLabel(text);
    l->setStyleSheet(QString("color:%1; font-size:10px; background:transparent;").arg(color.name()));
    return l;
}

QWidget* DashboardWidget::createMetricCard(const QString& label, QLabel** valueOut, QLabel** deltaOut, const QString& deltaColor) {
    auto* card = new QWidget;
    card->setObjectName("metricCard");

    auto* lay = new QVBoxLayout(card);
    lay->setContentsMargins(14, 12, 14, 12);
    lay->setSpacing(2);

    auto* titleLabel = new QLabel(label);
    titleLabel->setStyleSheet("color:rgba(255,255,255,0.5); font-size:10px; font-weight:600; letter-spacing:1px; background:transparent;");
    lay->addWidget(titleLabel);

    auto* row = new QHBoxLayout;
    row->setSpacing(2);
    *valueOut = new QLabel("—");
    (*valueOut)->setStyleSheet("font-size:28px; font-weight:700; color:white; background:transparent;");
    row->addWidget(*valueOut);

    auto* unitLabel = new QLabel(label.contains("потерь") ? "%" : "ms");
    unitLabel->setStyleSheet("color:rgba(255,255,255,0.4); font-size:13px; background:transparent;");
    unitLabel->setContentsMargins(0, 6, 0, 0);
    row->addWidget(unitLabel);
    row->addStretch();
    lay->addLayout(row);

    *deltaOut = new QLabel;
    (*deltaOut)->setStyleSheet(QString("color:%1; font-size:11px; background:transparent;").arg(deltaColor));
    lay->addWidget(*deltaOut);

    return card;
}

void DashboardWidget::setMetricColor(QLabel* label, double value, double good, double warn) {
    QColor c;
    if (value < good) c = QColor(theme::Colors::SUCCESS);
    else if (value < warn) c = QColor(theme::Colors::WARNING);
    else c = QColor(theme::Colors::ERROR);
    label->setStyleSheet(QString("font-size:28px; font-weight:700; color:%1; background:transparent;").arg(c.name()));
}

// ---------------------------------------------------------------------------
// DashboardWidget
// ---------------------------------------------------------------------------

DashboardWidget::DashboardWidget(QWidget* parent)
    : QWidget(parent) {

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(14);

    // header
    auto* headerRow = new QHBoxLayout;
    auto* title = new QLabel("Главная");
    title->setStyleSheet("font-size:20px; font-weight:700; color:white; background:transparent;");
    auto* subtitle = new QLabel("Текущее состояние сети (реальные замеры)");
    subtitle->setStyleSheet("color:rgba(255,255,255,0.4); font-size:12px; background:transparent;");
    headerRow->addWidget(title);
    headerRow->addStretch();
    headerRow->addWidget(subtitle);
    root->addLayout(headerRow);

    // metric cards
    auto* cardsRow = new QHBoxLayout;
    cardsRow->setSpacing(10);

    QLabel* pingDelta;
    auto* pingCard = createMetricCard("ПИНГ", &ping_value_, &pingDelta, "#22c55e");
    pingDelta->setText("ожидание данных…");

    QLabel* jitterDelta;
    auto* jitterCard = createMetricCard("ДЖИТТЕР", &jitter_value_, &jitterDelta, "#22c55e");
    jitterDelta->setText("стабильность соединения");

    QLabel* lossDelta;
    auto* lossCard = createMetricCard("ПОТЕРИ ПАКЕТОВ", &loss_value_, &lossDelta, "#22c55e");
    lossDelta->setText("% потерянных пакетов");

    QLabel* routeDelta;
    auto* routeCard = createMetricCard("ПОСЛЕДНИЙ ПИНГ", &route_value_, &routeDelta, "#06b6d4");
    routeDelta->setText("цель: 1.1.1.1");

    cardsRow->addWidget(pingCard);
    cardsRow->addWidget(jitterCard);
    cardsRow->addWidget(lossCard);
    cardsRow->addWidget(routeCard);
    root->addLayout(cardsRow);

    // graph
    graph_ = new PingGraphWidget;
    root->addWidget(graph_);

    // boost button
    boost_btn_ = new QPushButton("⚡  ВКЛЮЧИТЬ ОПТИМИЗАЦИЮ");
    boost_btn_->setObjectName("boostButton");
    boost_btn_->setFixedHeight(52);
    boost_btn_->setCursor(Qt::PointingHandCursor);
    connect(boost_btn_, &QPushButton::clicked, this, &DashboardWidget::onBoostClicked);
    root->addWidget(boost_btn_);

    // v2.8: RAM cleaner + timer resolution + startup counter
    auto* quickRow = new QHBoxLayout();
    ram_btn_ = new QPushButton(QString::fromUtf8(
        "\xD0\x9E\xD1%87\xD0%B8%D1%81%D1%82%D0%B8%D1\x82\xD1%8C\x20RAM"), this);
    ram_btn_->setObjectName("boostButton");
    ram_btn_->setFixedHeight(36);
    connect(ram_btn_, &QPushButton::clicked, this, [this]() {
        ram_btn_->setEnabled(false);
        auto stats = gno::fpsboost::cleanRam();
        ram_label_->setText(QString::fromUtf8(
            "\xD0%9E%D1\x81%D0%B2%D0%BE%D0%B1%D0%BE%D0%B6%D0%B4%D0%B5%D0%BD%D0%BE\x3A\x20%1 MB")
            .arg(static_cast<int>(stats.bytes_freed_estimate / (1024 * 1024))));
        ram_btn_->setEnabled(true);
    });
    ram_label_ = new QLabel(QStringLiteral(" "), this);
    quickRow->addWidget(ram_btn_);
    quickRow->addWidget(ram_label_, 1);

    timer_label_ = new QLabel(this);
    timer_label_->setStyleSheet(QString(
        "color:%1; font-size:11px; font-family:Consolas; background:transparent;")
        .arg(theme::Colors::TEXT_TERTIARY));
    {
        auto res = gno::fpsboost::currentTimerResolution();
        timer_label_->setText(QString::fromUtf8("\xD0%A2%D0%B0%D0%B9%D0%BC%D0%B5%D1%80\x3A\x20%1 ms")
            .arg(res / 10000.0, 0, 'f', 1));
    }

    startup_label_ = new QLabel(this);
    startup_label_->setStyleSheet(QString(
        "color:%1; font-size:11px; background:transparent;")
        .arg(theme::Colors::TEXT_TERTIARY));
    {
        auto progs = gno::fpsboost::enumStartupPrograms();
        int enabled = 0;
        for (const auto& p : progs) if (p.enabled) ++enabled;
        startup_label_->setText(QString::fromUtf8(
            "\xD0\x90%D0%B2%D1%82%D0%BE%D0%B7%D0%B0%D0%B3%D1%80%D1%83%D0%B7%D0%BA%D0%B0\x3A\x20%1")
            .arg(enabled));
    }

    root->addWidget(timer_label_);
    root->addWidget(startup_label_);

    // comparison measure button
    auto* measureRow = new QHBoxLayout;
    measure_btn_ = new QPushButton("📊  Сравнительный замер «до / после»");
    measure_btn_->setObjectName("sidebarButton");
    measure_btn_->setFixedHeight(40);
    measure_btn_->setCursor(Qt::PointingHandCursor);
    connect(measure_btn_, &QPushButton::clicked, this, &DashboardWidget::onMeasureClicked);
    measureRow->addWidget(measure_btn_);
    measureRow->addStretch();
    root->addLayout(measureRow);

    measure_timer_ = new QTimer(this);
    measure_timer_->setInterval(1000);
    connect(measure_timer_, &QTimer::timeout, this, &DashboardWidget::onMeasureTick);

    // recommendations card
    auto* recHeader = new QLabel("💡  РЕКОМЕНДАЦИИ");
    recHeader->setStyleSheet("color:#F59E0B; font-size:11px; font-weight:700; letter-spacing:1px; background:transparent;");
    root->addWidget(recHeader);

    rec_card_ = new QLabel;
    rec_card_->setObjectName("metricCard");
    rec_card_->setWordWrap(true);
    rec_card_->setStyleSheet("color:rgba(255,255,255,0.75); font-size:12px; padding:12px 14px;");
    root->addWidget(rec_card_);

    // info bar
    auto* info = new QLabel("Измерение пинга к 1.1.1.1 (Cloudflare) каждую секунду. График обновляется автоматически.");
    info->setStyleSheet("color:rgba(255,255,255,0.35); font-size:11px; background:transparent;");
    info->setAlignment(Qt::AlignCenter);
    root->addWidget(info);

    // connect to the shared monitoring service
    auto& svc = MonitoringService::instance();
    connect(&svc, &MonitoringService::pingUpdated, this, &DashboardWidget::onPingUpdated);
    connect(&svc, &MonitoringService::jitterUpdated, this, &DashboardWidget::onJitterUpdated);
    connect(&svc, &MonitoringService::lossUpdated, this, &DashboardWidget::onLossUpdated);
    connect(&svc, &MonitoringService::sessionRecorded, this, &DashboardWidget::refreshRecommendations);

    refreshRecommendations();
}

DashboardWidget::~DashboardWidget() = default;

void DashboardWidget::onPingUpdated(double ms) {
    if (ms >= 0) {
        last_ping_ = ms;
        ping_value_->setText(QString::number(static_cast<int>(ms)));
        graph_->addPingValue(ms);
        route_value_->setText(QString::number(static_cast<int>(ms)));
        setMetricColor(ping_value_, ms, 35, 50);
    } else {
        ping_value_->setText("тайм-аут");
        graph_->addPingValue(0.0);
    }
}

void DashboardWidget::onJitterUpdated(double ms) {
    jitter_value_->setText(QString::number(ms, 'f', 1));
    setMetricColor(jitter_value_, ms, 2, 5);
}

void DashboardWidget::onLossUpdated(double percent) {
    loss_value_->setText(QString::number(percent, 'f', 1));
    setMetricColor(loss_value_, percent, 0.1, 1.0);
}

void DashboardWidget::refreshRecommendations() {
    auto recs = MonitoringService::instance().getRecommendations();
    QString text;
    for (int i = 0; i < recs.size(); ++i) {
        text += QString("• %1").arg(recs[i]);
        if (i < recs.size() - 1) text += "\n";
    }
    rec_card_->setText(text);
}

void DashboardWidget::setConnected(bool connected) {
    boost_btn_->setEnabled(connected);
}

void DashboardWidget::onBoostClicked() {
    boosting_ = !boosting_;
    MonitoringService::instance().setBoostActive(boosting_);
    if (boosting_) {
        boost_btn_->setObjectName("boostButtonActive");
        boost_btn_->setText("■  ОТКЛЮЧИТЬ ОПТИМИЗАЦИЮ");
    } else {
        boost_btn_->setObjectName("boostButton");
        boost_btn_->setText("⚡  ВКЛЮЧИТЬ ОПТИМИЗАЦИЮ");
    }
    boost_btn_->style()->unpolish(boost_btn_);
    boost_btn_->style()->polish(boost_btn_);
    emit boostToggled(boosting_);
}

// ---------------------------------------------------------------------------
// Comparison measurement: 30 s without optimization → 30 s with → PNG report
// ---------------------------------------------------------------------------

void DashboardWidget::onMeasureClicked() {
    if (measure_phase_ != 0)
        return;

    auto& svc = MonitoringService::instance();
    measure_before_.clear();
    measure_after_.clear();
    measure_seconds_ = 0;
    measure_phase_ = 1;

    svc.startMeasure(QString::fromUtf8("Без оптимизации"));
    measure_btn_->setEnabled(false);
    measure_btn_->setText(QString::fromUtf8("⏳ Измеряем без оптимизации… 30 с"));
    measure_timer_->start();
}

void DashboardWidget::onMeasureTick() {
    auto& svc = MonitoringService::instance();
    ++measure_seconds_;
    int remaining = kMeasureDuration - measure_seconds_;

    if (measure_phase_ == 1) {
        if (svc.hasPing())
            measure_before_.append(svc.currentPing());
        measure_btn_->setText(QString::fromUtf8("⏳ Измеряем без оптимизации… %1 с").arg(remaining));
        if (measure_seconds_ >= kMeasureDuration) {
            // phase 1 done → enable boost
            measure_phase_ = 2;
            measure_seconds_ = 0;
            if (!boosting_)
                emit boostToggled(true);   // main_gui applies real optimizations
            svc.startMeasure(QString::fromUtf8("С оптимизацией"));
            measure_btn_->setText(QString::fromUtf8("⚡ Измеряем с оптимизацией… 30 с"));
        }
    } else if (measure_phase_ == 2) {
        if (svc.hasPing())
            measure_after_.append(svc.currentPing());
        measure_btn_->setText(QString::fromUtf8("⚡ Измеряем с оптимизацией… %1 с").arg(remaining));
        if (measure_seconds_ >= kMeasureDuration) {
            measure_timer_->stop();
            measure_phase_ = 0;
            svc.stopMeasure();

            // disable boost again (main_gui reverts optimizations)
            if (boosting_)
                emit boostToggled(false);

            measure_btn_->setEnabled(true);
            measure_btn_->setText("📊  Сравнительный замер «до / после»");

            auto avgOf = [](const QVector<double>& v) {
                if (v.isEmpty()) return 0.0;
                double s = 0.0;
                for (double x : v) s += x;
                return s / v.size();
            };

            QString dir = report::defaultReportsDir();
            QString path = QFileDialog::getSaveFileName(
                this, QString::fromUtf8("Сохранить отчёт"),
                dir + QString("/E2E4-comparison-%1.png").arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")),
                "PNG (*.png)");
            if (path.isEmpty())
                return;

            bool ok = report::exportComparisonReport(
                path,
                QString::fromUtf8("Без оптимизации"), avgOf(measure_before_),
                MonitoringService::instance().currentJitter(), 0.0,
                static_cast<uint32_t>(measure_before_.size()),
                QString::fromUtf8("С оптимизацией"), avgOf(measure_after_),
                MonitoringService::instance().currentJitter(), 0.0,
                static_cast<uint32_t>(measure_after_.size()));

            if (ok) {
                QMessageBox::information(
                    this, QString::fromUtf8("Отчёт сохранён"),
                    QString::fromUtf8("Отчёт «до/после» сохранён:\n%1\n\nОткрыть файл?").arg(path));
                QDesktopServices::openUrl(QUrl::fromLocalFile(path));
            } else {
                QMessageBox::warning(this, QString::fromUtf8("Ошибка"),
                                     QString::fromUtf8("Не удалось сохранить отчёт."));
            }
        }
    }
}

} // namespace gno