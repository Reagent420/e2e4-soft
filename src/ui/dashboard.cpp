#include "dashboard.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QRandomGenerator>
#include <QStyle>
#include <QFont>

// ---------------------------------------------------------------------------
// PingGraphWidget
// ---------------------------------------------------------------------------

PingGraphWidget::PingGraphWidget(QWidget* parent)
    : QWidget(parent) {
    setObjectName("metricCard");
    setFixedHeight(180);
    for (int i = 0; i < kMaxPoints; ++i)
        data_.append(32.0);
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
        double y = area.bottom() - (data_[i] / 100.0) * area.height();
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
    *valueOut = new QLabel("0");
    (*valueOut)->setStyleSheet("font-size:28px; font-weight:700; color:white; background:transparent;");
    row->addWidget(*valueOut);

    auto* unitLabel = new QLabel("ms");
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
    auto* title = new QLabel("Dashboard");
    title->setStyleSheet("font-size:20px; font-weight:700; color:white; background:transparent;");
    auto* subtitle = new QLabel("Real-time network status");
    subtitle->setStyleSheet("color:rgba(255,255,255,0.4); font-size:12px; background:transparent;");
    headerRow->addWidget(title);
    headerRow->addStretch();
    headerRow->addWidget(subtitle);
    root->addLayout(headerRow);

    // metric cards
    auto* cardsRow = new QHBoxLayout;
    cardsRow->setSpacing(10);

    QLabel* pingDelta;
    auto* pingCard = createMetricCard("CURRENT PING", &ping_value_, &pingDelta, "#22c55e");
    pingDelta->setText("▼15ms from baseline");

    QLabel* jitterDelta;
    auto* jitterCard = createMetricCard("JITTER", &jitter_value_, &jitterDelta, "#22c55e");
    jitterDelta->setText("Good");

    QLabel* lossDelta;
    auto* lossCard = createMetricCard("PACKET LOSS", &loss_value_, &lossDelta, "#22c55e");
    lossDelta->setText("Perfect");

    QLabel* routeDelta;
    auto* routeCard = createMetricCard("ACTIVE ROUTES", &route_value_, &routeDelta, "#06b6d4");
    routeCard->findChild<QLabel*>({}, Qt::FindChildrenRecursively);
    routeDelta->setText("Best path");

    cardsRow->addWidget(pingCard);
    cardsRow->addWidget(jitterCard);
    cardsRow->addWidget(lossCard);
    cardsRow->addWidget(routeCard);
    root->addLayout(cardsRow);

    // graph
    graph_ = new PingGraphWidget;
    root->addWidget(graph_);

    // boost button
    boost_btn_ = new QPushButton("⚡  BOOST CONNECTION");
    boost_btn_->setObjectName("boostButton");
    boost_btn_->setFixedHeight(52);
    boost_btn_->setCursor(Qt::PointingHandCursor);
    connect(boost_btn_, &QPushButton::clicked, this, &DashboardWidget::onBoostClicked);
    root->addWidget(boost_btn_);

    // info bar
    auto* info = new QLabel("Current Game: Counter-Strike 2  |  Server: EU West  |  Node: Frankfurt");
    info->setStyleSheet("color:rgba(255,255,255,0.35); font-size:11px; background:transparent;");
    info->setAlignment(Qt::AlignCenter);
    root->addWidget(info);

    // timer
    refresh_timer_ = new QTimer(this);
    connect(refresh_timer_, &QTimer::timeout, this, &DashboardWidget::onRefresh);
    refresh_timer_->start(1000);
}

// slots ---------------------------------------------------------------

void DashboardWidget::updatePing(double ms) {
    ping_value_->setText(QString::number(static_cast<int>(ms)));
    graph_->addPingValue(ms);
}

void DashboardWidget::updateJitter(double ms) {
    jitter_value_->setText(QString::number(ms, 'f', 1));
}

void DashboardWidget::updatePacketLoss(double percent) {
    loss_value_->setText(QString::number(percent, 'f', 1));
}

void DashboardWidget::updateRouteCount(int count) {
    route_value_->setText(QString::number(count));
}

void DashboardWidget::setConnected(bool connected) {
    boost_btn_->setEnabled(connected);
}

void DashboardWidget::onBoostClicked() {
    boosting_ = !boosting_;
    if (boosting_) {
        boost_btn_->setObjectName("boostButtonActive");
        boost_btn_->setText("■  DISCONNECT");
    } else {
        boost_btn_->setObjectName("boostButton");
        boost_btn_->setText("⚡  BOOST CONNECTION");
    }
    boost_btn_->style()->unpolish(boost_btn_);
    boost_btn_->style()->polish(boost_btn_);
    emit boostToggled(boosting_);
}

void DashboardWidget::onRefresh() {
    auto* rng = QRandomGenerator::global();
    updatePing(rng->bounded(28, 46));
    updateJitter(rng->bounded(10, 35) / 10.0);
    updatePacketLoss(rng->bounded(0, 5) / 10.0);
    updateRouteCount(rng->bounded(2, 6));
}
