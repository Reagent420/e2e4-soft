#include "monitoring.h"
#include "theme.h"
#include "../monitoring/ping_monitor.h"

#include <QFrame>
#include <QDateTime>
#include <QScrollBar>

#include <atomic>

// ============================================================================
// PingChartWidget
// ============================================================================

PingChartWidget::PingChartWidget(QWidget* parent) : QWidget(parent) {
    setObjectName("metricCard");
    setFixedHeight(200);
    setMinimumWidth(250);
    for (int i = 0; i < kMaxDataPoints; ++i) data_.append(0);
}

void PingChartWidget::addPoint(double value) {
    data_.append(value);
    if (data_.size() > kMaxDataPoints) data_.removeFirst();
    update();
}

void PingChartWidget::drawGrid(QPainter& p, const QRect& r) {
    p.setPen(QPen(QColor("#1E293B"), 1));
    for (int i = 0; i <= 4; ++i) {
        int y = r.top() + (r.height() * i) / 4;
        p.drawLine(r.left(), y, r.right(), y);
    }
    QFont f = p.font();
    f.setPixelSize(9);
    p.setFont(f);
    p.setPen(QColor("#64748B"));
    for (int i = 0; i <= 4; ++i) {
        int y = r.top() + (r.height() * i) / 4;
        int val = 100 - (100 * i) / 4;
        p.drawText(r.left() + 4, y - 3, QString::number(val) + "ms");
    }
}

void PingChartWidget::drawChart(QPainter& p, const QRect& r) {
    if (data_.size() < 2) return;
    double maxVal = 100.0;
    int n = data_.size();
    double step = r.width() / static_cast<double>(n - 1);

    QPainterPath fillPath;
    QPointF first(r.left(), r.top() + r.height() * (1.0 - qBound(0.0, data_[0], maxVal) / maxVal));
    fillPath.moveTo(first);

    QPolygonF poly;
    poly << first;
    for (int i = 1; i < n; ++i) {
        QPointF pt(r.left() + i * step, r.top() + r.height() * (1.0 - qBound(0.0, data_[i], maxVal) / maxVal));
        QPointF prev(r.left() + (i - 1) * step, r.top() + r.height() * (1.0 - qBound(0.0, data_[i - 1], maxVal) / maxVal));
        QPointF c1(prev.x() + step * 0.4, prev.y());
        QPointF c2(pt.x() - step * 0.4, pt.y());
        fillPath.cubicTo(c1, c2, pt);
        poly << pt;
    }

    fillPath.lineTo(r.right(), r.bottom());
    fillPath.lineTo(r.left(), r.bottom());
    fillPath.closeSubpath();

    QLinearGradient grad(r.topLeft(), r.bottomLeft());
    grad.setColorAt(0, QColor(59, 130, 246, 60));
    grad.setColorAt(1, QColor(59, 130, 246, 5));
    p.fillPath(fillPath, grad);

    p.setPen(QPen(QColor("#3B82F6"), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolyline(poly);
}

void PingChartWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QRect content(12, 8, width() - 24, height() - 16);
    QFont tf = p.font();
    tf.setPixelSize(11);
    tf.setBold(true);
    p.setFont(tf);
    p.setPen(QColor("#94A3B8"));
    p.drawText(content, Qt::AlignTop | Qt::AlignLeft, QString::fromUtf8("ПИНГ (мс)"));

    double cur = data_.isEmpty() ? 0 : data_.last();
    QFont vf = p.font();
    vf.setPixelSize(22);
    vf.setBold(true);
    p.setFont(vf);
    QColor vc = cur < 35 ? QColor("#22C55E") : cur < 50 ? QColor("#EAB308") : QColor("#EF4444");
    p.setPen(vc);
    p.drawText(content, Qt::AlignTop | Qt::AlignRight, QString::number(cur, 'f', 1) + "ms");

    QRect chartRect(content.left(), content.top() + 28, content.width(), content.height() - 32);
    drawGrid(p, chartRect);
    drawChart(p, chartRect);
}

// ============================================================================
// JitterChartWidget
// ============================================================================

JitterChartWidget::JitterChartWidget(QWidget* parent) : QWidget(parent) {
    setObjectName("metricCard");
    setFixedHeight(200);
    setMinimumWidth(250);
    for (int i = 0; i < kMaxDataPoints; ++i) data_.append(0);
}

void JitterChartWidget::addPoint(double value) {
    data_.append(value);
    if (data_.size() > kMaxDataPoints) data_.removeFirst();
    update();
}

void JitterChartWidget::drawGrid(QPainter& p, const QRect& r) {
    p.setPen(QPen(QColor("#1E293B"), 1));
    for (int i = 0; i <= 4; ++i) {
        int y = r.top() + (r.height() * i) / 4;
        p.drawLine(r.left(), y, r.right(), y);
    }
    QFont f = p.font();
    f.setPixelSize(9);
    p.setFont(f);
    p.setPen(QColor("#64748B"));
    for (int i = 0; i <= 4; ++i) {
        int y = r.top() + (r.height() * i) / 4;
        double val = 20.0 - (20.0 * i) / 4;
        p.drawText(r.left() + 4, y - 3, QString::number(val, 'f', 0) + "ms");
    }
}

void JitterChartWidget::drawChart(QPainter& p, const QRect& r) {
    if (data_.size() < 2) return;
    double maxVal = 20.0;
    int n = data_.size();
    double step = r.width() / static_cast<double>(n - 1);

    QPainterPath fillPath;
    QPointF first(r.left(), r.top() + r.height() * (1.0 - qBound(0.0, data_[0], maxVal) / maxVal));
    fillPath.moveTo(first);

    QPolygonF poly;
    poly << first;
    for (int i = 1; i < n; ++i) {
        QPointF pt(r.left() + i * step, r.top() + r.height() * (1.0 - qBound(0.0, data_[i], maxVal) / maxVal));
        QPointF prev(r.left() + (i - 1) * step, r.top() + r.height() * (1.0 - qBound(0.0, data_[i - 1], maxVal) / maxVal));
        QPointF c1(prev.x() + step * 0.4, prev.y());
        QPointF c2(pt.x() - step * 0.4, pt.y());
        fillPath.cubicTo(c1, c2, pt);
        poly << pt;
    }

    fillPath.lineTo(r.right(), r.bottom());
    fillPath.lineTo(r.left(), r.bottom());
    fillPath.closeSubpath();

    QLinearGradient grad(r.topLeft(), r.bottomLeft());
    grad.setColorAt(0, QColor(6, 182, 212, 60));
    grad.setColorAt(1, QColor(6, 182, 212, 5));
    p.fillPath(fillPath, grad);

    p.setPen(QPen(QColor("#06B6D4"), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolyline(poly);
}

void JitterChartWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QRect content(12, 8, width() - 24, height() - 16);
    QFont tf = p.font();
    tf.setPixelSize(11);
    tf.setBold(true);
    p.setFont(tf);
    p.setPen(QColor("#94A3B8"));
    p.drawText(content, Qt::AlignTop | Qt::AlignLeft, QString::fromUtf8("ДЖИТТЕР (мс)"));

    double cur = data_.isEmpty() ? 0 : data_.last();
    QFont vf = p.font();
    vf.setPixelSize(22);
    vf.setBold(true);
    p.setFont(vf);
    QColor vc = cur < 2 ? QColor("#22C55E") : cur < 5 ? QColor("#EAB308") : QColor("#EF4444");
    p.setPen(vc);
    p.drawText(content, Qt::AlignTop | Qt::AlignRight, QString::number(cur, 'f', 1) + "ms");

    QRect chartRect(content.left(), content.top() + 28, content.width(), content.height() - 32);
    drawGrid(p, chartRect);
    drawChart(p, chartRect);
}

// ============================================================================
// PacketLossChartWidget
// ============================================================================

PacketLossChartWidget::PacketLossChartWidget(QWidget* parent) : QWidget(parent) {
    setObjectName("metricCard");
    setFixedHeight(200);
    setMinimumWidth(250);
    for (int i = 0; i < kMaxDataPoints; ++i) data_.append(0);
}

void PacketLossChartWidget::addPoint(double value) {
    data_.append(value);
    if (data_.size() > kMaxDataPoints) data_.removeFirst();
    update();
}

void PacketLossChartWidget::drawGrid(QPainter& p, const QRect& r) {
    p.setPen(QPen(QColor("#1E293B"), 1));
    for (int i = 0; i <= 4; ++i) {
        int y = r.top() + (r.height() * i) / 4;
        p.drawLine(r.left(), y, r.right(), y);
    }
    QFont f = p.font();
    f.setPixelSize(9);
    p.setFont(f);
    p.setPen(QColor("#64748B"));
    for (int i = 0; i <= 4; ++i) {
        int y = r.top() + (r.height() * i) / 4;
        double val = 10.0 - (10.0 * i) / 4;
        p.drawText(r.left() + 4, y - 3, QString::number(val, 'f', 0) + "%");
    }
}

void PacketLossChartWidget::drawChart(QPainter& p, const QRect& r) {
    if (data_.isEmpty()) return;
    double maxVal = 10.0;
    int n = data_.size();
    double barWidth = qMax(1.0, r.width() / static_cast<double>(n) * 0.7);
    double gap = r.width() / static_cast<double>(n);

    for (int i = 0; i < n; ++i) {
        double val = data_[i];
        int barH = static_cast<int>(r.height() * val / maxVal);
        if (barH < 1 && val > 0) barH = 1;
        int x = static_cast<int>(r.left() + i * gap + (gap - barWidth) / 2);
        int y = r.bottom() - barH;

        QColor barColor = val > 0 ? QColor("#EF4444") : QColor("#22C55E");
        QColor barFill = val > 0 ? QColor(239, 68, 68, 160) : QColor(34, 197, 94, 80);
        p.setPen(QPen(barColor, 1));
        p.setBrush(barFill);
        p.drawRoundedRect(x, y, static_cast<int>(barWidth), barH, 2, 2);
    }
}

void PacketLossChartWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QRect content(12, 8, width() - 24, height() - 16);
    QFont tf = p.font();
    tf.setPixelSize(11);
    tf.setBold(true);
    p.setFont(tf);
    p.setPen(QColor("#94A3B8"));
    p.drawText(content, Qt::AlignTop | Qt::AlignLeft, QString::fromUtf8("ПОТЕРИ ПАКЕТОВ (%)"));

    double cur = data_.isEmpty() ? 0 : data_.last();
    QFont vf = p.font();
    vf.setPixelSize(22);
    vf.setBold(true);
    p.setFont(vf);
    QColor vc = cur < 0.1 ? QColor("#22C55E") : cur < 1.0 ? QColor("#EAB308") : QColor("#EF4444");
    p.setPen(vc);
    p.drawText(content, Qt::AlignTop | Qt::AlignRight, QString::number(cur, 'f', 2) + "%");

    QRect chartRect(content.left(), content.top() + 28, content.width(), content.height() - 32);
    drawGrid(p, chartRect);
    drawChart(p, chartRect);
}

// ============================================================================
// BandwidthChartWidget
// ============================================================================

BandwidthChartWidget::BandwidthChartWidget(QWidget* parent) : QWidget(parent) {
    setObjectName("metricCard");
    setFixedHeight(200);
    setMinimumWidth(250);
    for (int i = 0; i < kMaxDataPoints; ++i) {
        download_data_.append(0);
        upload_data_.append(0);
    }
}

void BandwidthChartWidget::addDownloadPoint(double value) {
    download_data_.append(value);
    if (download_data_.size() > kMaxDataPoints) download_data_.removeFirst();
    update();
}

void BandwidthChartWidget::addUploadPoint(double value) {
    upload_data_.append(value);
    if (upload_data_.size() > kMaxDataPoints) upload_data_.removeFirst();
    update();
}

void BandwidthChartWidget::drawGrid(QPainter& p, const QRect& r) {
    p.setPen(QPen(QColor("#1E293B"), 1));
    for (int i = 0; i <= 4; ++i) {
        int y = r.top() + (r.height() * i) / 4;
        p.drawLine(r.left(), y, r.right(), y);
    }
    QFont f = p.font();
    f.setPixelSize(9);
    p.setFont(f);
    p.setPen(QColor("#64748B"));
    for (int i = 0; i <= 4; ++i) {
        int y = r.top() + (r.height() * i) / 4;
        int val = 100 - (100 * i) / 4;
        p.drawText(r.left() + 4, y - 3, QString::number(val) + "М");
    }
}

void BandwidthChartWidget::drawChart(QPainter& p, const QRect& r) {
    double maxVal = 100.0;
    int n = download_data_.size();
    if (n < 2) return;
    double step = r.width() / static_cast<double>(n - 1);

    auto drawArea = [&](const QVector<double>& data, QColor lineColor, QColor fillColorTop) {
        QPainterPath fillPath;
        QPointF first(r.left(), r.top() + r.height() * (1.0 - qBound(0.0, data[0], maxVal) / maxVal));
        fillPath.moveTo(first);

        QPolygonF poly;
        poly << first;
        for (int i = 1; i < n; ++i) {
            QPointF pt(r.left() + i * step, r.top() + r.height() * (1.0 - qBound(0.0, data[i], maxVal) / maxVal));
            QPointF prev(r.left() + (i - 1) * step, r.top() + r.height() * (1.0 - qBound(0.0, data[i - 1], maxVal) / maxVal));
            QPointF c1(prev.x() + step * 0.4, prev.y());
            QPointF c2(pt.x() - step * 0.4, pt.y());
            fillPath.cubicTo(c1, c2, pt);
            poly << pt;
        }

        fillPath.lineTo(r.right(), r.bottom());
        fillPath.lineTo(r.left(), r.bottom());
        fillPath.closeSubpath();

        QLinearGradient grad(r.topLeft(), r.bottomLeft());
        grad.setColorAt(0, fillColorTop);
        grad.setColorAt(1, QColor(fillColorTop.red(), fillColorTop.green(), fillColorTop.blue(), 5));
        p.fillPath(fillPath, grad);

        p.setPen(QPen(lineColor, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPolyline(poly);
    };

    drawArea(download_data_, QColor("#3B82F6"), QColor(59, 130, 246, 60));
    drawArea(upload_data_, QColor("#8B5CF6"), QColor(139, 92, 246, 60));
}

void BandwidthChartWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QRect content(12, 8, width() - 24, height() - 16);
    QFont tf = p.font();
    tf.setPixelSize(11);
    tf.setBold(true);
    p.setFont(tf);
    p.setPen(QColor("#94A3B8"));
    p.drawText(content.left(), content.top(), content.width() / 2, 18, Qt::AlignTop | Qt::AlignLeft, QString::fromUtf8("СКОРОСТЬ (Мбит/с)"));

    double dl = download_data_.isEmpty() ? 0 : download_data_.last();
    double ul = upload_data_.isEmpty() ? 0 : upload_data_.last();

    QFont vf = p.font();
    vf.setPixelSize(10);
    p.setFont(vf);
    int legendX = content.right() - 160;
    p.setPen(QColor("#3B82F6"));
    p.drawText(legendX, content.top() + 2, 80, 16, Qt::AlignVCenter | Qt::AlignLeft,
               QString::fromUtf8("\u2193 Загрузка") + " " + QString::number(dl, 'f', 0));
    p.setPen(QColor("#8B5CF6"));
    p.drawText(legendX + 80, content.top() + 2, 80, 16, Qt::AlignVCenter | Qt::AlignLeft,
               QString::fromUtf8("\u2191 Отдача") + " " + QString::number(ul, 'f', 0));

    QRect chartRect(content.left(), content.top() + 28, content.width(), content.height() - 32);
    drawGrid(p, chartRect);
    drawChart(p, chartRect);
}

// ============================================================================
// MonitoringWidget
// ============================================================================

namespace gno {

MonitoringWidget::MonitoringWidget(QWidget* parent) : QWidget(parent) {
    setObjectName("monitoringPage");

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 20, 24, 20);
    mainLayout->setSpacing(16);

    // Header
    auto* headerRow = new QHBoxLayout();
    auto* titleLabel = new QLabel(QString::fromUtf8("Мониторинг сети"));
    titleLabel->setObjectName("pageTitle");
    QFont titleFont;
    titleFont.setPixelSize(20);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    titleLabel->setStyleSheet("color: #F1F5F9;");

    auto* subtitleLabel = new QLabel(QString::fromUtf8("Реальные замеры каждую секунду — цель 1.1.1.1"));
    subtitleLabel->setObjectName("pageSubtitle");
    QFont subFont;
    subFont.setPixelSize(13);
    subtitleLabel->setFont(subFont);
    subtitleLabel->setStyleSheet("color: #64748B;");

    headerRow->addWidget(titleLabel);
    headerRow->addStretch();
    headerRow->addWidget(subtitleLabel);
    mainLayout->addLayout(headerRow);

    // 2x2 chart grid
    auto* grid = new QGridLayout();
    grid->setSpacing(12);

    ping_chart_ = new PingChartWidget(this);
    jitter_chart_ = new JitterChartWidget(this);
    loss_chart_ = new PacketLossChartWidget(this);
    bw_chart_ = new BandwidthChartWidget(this);

    grid->addWidget(ping_chart_, 0, 0);
    grid->addWidget(jitter_chart_, 0, 1);
    grid->addWidget(loss_chart_, 1, 0);
    grid->addWidget(bw_chart_, 1, 1);

    mainLayout->addLayout(grid);

    // Connection log
    auto* logHeader = new QLabel(QString::fromUtf8("ЖУРНАЛ СОБЫТИЙ"));
    logHeader->setObjectName("sectionTitle");
    QFont logFont;
    logFont.setPixelSize(11);
    logFont.setBold(true);
    logHeader->setFont(logFont);
    logHeader->setStyleSheet("color: #94A3B8; padding-top: 4px;");
    mainLayout->addWidget(logHeader);

    log_container_ = new QWidget();
    log_container_->setStyleSheet("background: #0F172A; border: 1px solid #1E293B; border-radius: 8px;");
    log_layout_ = new QVBoxLayout(log_container_);
    log_layout_->setContentsMargins(12, 8, 12, 8);
    log_layout_->setSpacing(2);
    log_layout_->addStretch();

    log_scroll_ = new QScrollArea();
    log_scroll_->setWidget(log_container_);
    log_scroll_->setWidgetResizable(true);
    log_scroll_->setFixedHeight(140);
    log_scroll_->setStyleSheet("QScrollArea { background: #0F172A; border: 1px solid #1E293B; border-radius: 8px; }"
                               "QScrollBar:vertical { background: #1E293B; width: 8px; }"
                               "QScrollBar::handle:vertical { background: #334155; border-radius: 4px; min-height: 20px; }"
                               "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }");
    mainLayout->addWidget(log_scroll_);

    // Real monitoring: ping callback drives the charts
    ping_monitor_.setPingCallback([this](const ICMPResult& result) {
        double ping = result.success ? result.latency_ms : -1.0;

        if (ping >= 0) {
            // jitter: deviation over last 10 samples
            jitter_samples_.append(ping);
            if (jitter_samples_.size() > 10) jitter_samples_.removeFirst();
            double jitter = 0.0;
            if (jitter_samples_.size() >= 2) {
                double avg = 0;
                for (double v : jitter_samples_) avg += v;
                avg /= jitter_samples_.size();
                double dev = 0;
                for (double v : jitter_samples_) dev += qAbs(v - avg);
                jitter = dev / jitter_samples_.size();
            }

            // loss: 0 or 1 per second, smoothed over 30s window
            loss_window_.append(0.0);
            while (loss_window_.size() > 30) loss_window_.removeFirst();
            double lossSum = 0;
            for (double v : loss_window_) lossSum += v;
            double lossPercent = lossSum / loss_window_.size() * 100.0;

            ping_chart_->addPoint(ping);
            jitter_chart_->addPoint(jitter);
            loss_chart_->addPoint(lossPercent);

            QString time = QDateTime::currentDateTime().toString("HH:mm:ss");
            QColor pingColor = ping < 35 ? QColor("#22C55E") : ping < 50 ? QColor("#EAB308") : QColor("#EF4444");
            QString msg = QString("[%1] Пинг: <font color='%2'>%3мс</font>")
                              .arg(time)
                              .arg(pingColor.name())
                              .arg(QString::number(ping, 'f', 1));
            addLogEntry(msg);
        } else {
            loss_window_.append(1.0);
            while (loss_window_.size() > 30) loss_window_.removeFirst();
            double lossSum = 0;
            for (double v : loss_window_) lossSum += v;
            double lossPercent = lossSum / loss_window_.size() * 100.0;

            ping_chart_->addPoint(0.0);
            loss_chart_->addPoint(lossPercent);

            QString time = QDateTime::currentDateTime().toString("HH:mm:ss");
            addLogEntry(QString("[%1] <font color='#EF4444'>Тайм-аут — пакет потерян</font>").arg(time));
        }
    });
    ping_monitor_.start("1.1.1.1", 1000);
}

void MonitoringWidget::addLogEntry(const QString& message, const QColor& color) {
    Q_UNUSED(color);
    auto* label = new QLabel(message);
    label->setStyleSheet("color: #94A3B8; font-size: 11px; font-family: 'Cascadia Code', 'Consolas', monospace; padding: 1px 0;");

    log_layout_->insertWidget(log_layout_->count() - 1, label);
    log_count_++;

    while (log_count_ > 100) {
        QLayoutItem* item = log_layout_->takeAt(0);
        if (item && item->widget()) {
            item->widget()->deleteLater();
            delete item;
        }
        log_count_--;
    }

    QMetaObject::invokeMethod(log_scroll_, [this]() {
        log_scroll_->verticalScrollBar()->setValue(log_scroll_->verticalScrollBar()->maximum());
    }, Qt::QueuedConnection);
}

} // namespace gno