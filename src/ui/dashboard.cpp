#include "dashboard.h"
#include "theme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QFont>
#include <QPointer>

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

    // info bar
    auto* info = new QLabel("Измерение пинга к 1.1.1.1 (Cloudflare) каждую секунду. График обновляется автоматически.");
    info->setStyleSheet("color:rgba(255,255,255,0.35); font-size:11px; background:transparent;");
    info->setAlignment(Qt::AlignCenter);
    root->addWidget(info);

    // monitoring
    startMonitoring();
}

DashboardWidget::~DashboardWidget() {
    ping_monitor_.stop();
}

void DashboardWidget::startMonitoring() {
    QPointer<DashboardWidget> owner(this);
    ping_monitor_.setPingCallback([owner](const ICMPResult& result) {
        if (!owner) {
            return;
        }
        QMetaObject::invokeMethod(owner.data(), [owner, result]() {
            if (owner) {
                owner->onPingResult(result);
            }
        }, Qt::QueuedConnection);
    });
    ping_monitor_.start("1.1.1.1", 1000);
}

void DashboardWidget::onPingResult(const ICMPResult& result) {
    double ping = result.success ? result.latency_ms : -1.0;

    if (ping >= 0) {
        last_ping_ = ping;
        ping_value_->setText(QString::number(static_cast<int>(ping)));
        graph_->addPingValue(ping);
        route_value_->setText(QString::number(static_cast<int>(ping)));

        // jitter: среднее отклонение от последних 10 замеров
        jitter_history_.append(ping);
        if (jitter_history_.size() > 10) jitter_history_.removeFirst();
        if (jitter_history_.size() >= 2) {
            double avg = 0;
            for (double v : jitter_history_) avg += v;
            avg /= jitter_history_.size();
            double dev = 0;
            for (double v : jitter_history_) dev += qAbs(v - avg);
            dev /= jitter_history_.size();
            jitter_value_->setText(QString::number(dev, 'f', 1));
        }

        // loss: считаем по пропущенным пингам
        ++packets_sent_;
        loss_history_.append(0.0);
    } else {
        ++packets_sent_;
        ++packets_lost_;
        loss_history_.append(1.0);
        ping_value_->setText("тайм-аут");
        graph_->addPingValue(0.0);
    }

    if (loss_history_.size() > 30) loss_history_.removeFirst();
    double lossSum = 0;
    for (double v : loss_history_) lossSum += v;
    double lossPercent = lossSum / qMax(1, loss_history_.size()) * 100.0;
    loss_value_->setText(QString::number(lossPercent, 'f', 1));
}

} // namespace gno
