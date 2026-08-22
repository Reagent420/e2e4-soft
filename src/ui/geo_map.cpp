#include "geo_map.h"
#include "theme.h"
#include "core/speed_test.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QSettings>

#include <atomic>
#include <cmath>
#include <thread>

namespace gno {

namespace {

QString gradeTag(int latency) {
    switch (ServerMapModel::grade(latency)) {
        case MapGrade::Good:   return QString::fromUtf8("[OK] ");
        case MapGrade::Medium: return QString::fromUtf8("[MED]");
        case MapGrade::Bad:    return QString::fromUtf8("[BAD]");
        default:               return QStringLiteral("[?]  ");
    }
}

QColor gradeColor(int latency, bool* is_neon = nullptr) {
    if (is_neon) *is_neon = false;
    switch (ServerMapModel::grade(latency)) {
        case MapGrade::Good:   return QColor(theme::Colors::SUCCESS);
        case MapGrade::Medium: return QColor(theme::Colors::WARNING);
        case MapGrade::Bad:    return QColor(0xFF, 0x2E, 0x88);
        default:               break;
    }
    if (is_neon) *is_neon = true;
    return QColor("#00F0FF");
}

} // namespace

GeoMapWidget::GeoMapWidget(QWidget* parent) : QWidget(parent) {
    SpeedTest speedtest;
    for (const auto& n : speedtest.getServers()) {
        servers_.push_back(MapServer{n.name, n.city, n.country, n.ip, n.latitude, n.longitude, -1});
    }
    setupUI();
    loadSettings();
    rebuildVisibleServers();
    updateDetailsCard();

    m_timer_ = new QTimer(this);
    connect(m_timer_, &QTimer::timeout, this, [this]() {
        if (isVisible() && !m_check_all_btn_->isEnabled()) return;
        if (isVisible()) onCheckAllClicked();
    });

    connect(this, &GeoMapWidget::probeFinished, this, [this]() {
        best_ = ServerMapModel::bestServer(servers_);
        rebuildVisibleServers();
        updateDetailsCard();
        update();
    });
    connect(this, &GeoMapWidget::probeProgress, this,
            [this](const QString& text) { m_progress_->setText(text); }, Qt::QueuedConnection);
}

void GeoMapWidget::setupUI() {
    setObjectName("geoMapPage");
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(24, 20, 24, 20);
    root->setSpacing(16);

    // ---- controls column -------------------------------------------------
    auto* controls = new QVBoxLayout();
    controls->setSpacing(10);

    auto* title = new QLabel(QString::fromUtf8(
        "\xD0\x9A\xD0\x90\xD0\xA0\xD0\xA2\xD0\x90\x20\xD0\xA1\xD0\x95\xD0\xA0\xD0\x92\xD0\x95\xD0\xA0\xD0\x9E\xD0\x92"), this);
    title->setStyleSheet(QString("font-size:18px; font-weight:700; letter-spacing:2px; color:%1;"
                                 "background:transparent;")
                             .arg(theme::Colors::ACCENT_NEON));

    auto* regionLbl = new QLabel(QString::fromUtf8(
        "\xD0\xA0\xD0\xB5\xD0\xB3\xD0\xB8\xD0\xBE\xD0\xBD"), this);
    regionLbl->setStyleSheet(QString("color:%1; background:transparent;").arg(theme::Colors::TEXT_SECONDARY));
    m_region_ = new QComboBox(this);
    m_region_->addItem(QString::fromUtf8("\xD0\x92\xD1\x81\xD0\xB5"), "all");
    m_region_->addItem("EU", "EU");
    m_region_->addItem("NA", "NA");
    m_region_->addItem("ASIA", "ASIA");
    m_region_->addItem("SA", "SA");
    m_region_->addItem("OCE", "OCE");

    m_labels_ = new QCheckBox(QString::fromUtf8(
        "\xD0\x9F\xD0\xBE\xD0\xB4\xD0\xBF\xD0\xB8\xD1\x81\xD0\xB8\x20\xD1\x83\xD0\xB7\xD0\xBB\xD0\xBE\xD0\xB2"), this);
    m_grid_ = new QCheckBox(QString::fromUtf8(
        "\xD0\xA1\xD0\xB5\xD1\x82\xD0\xBA\xD0\xB0\x20\xD0\xBA\xD0\xBE\xD0\xBE\xD1\x80\xD0\xB4\xD0\xB8\xD0\xBD\xD0\xB0\xD1\x82"), this);
    m_labels_->setChecked(true);
    m_grid_->setChecked(true);

    auto* intervalLbl = new QLabel(QString::fromUtf8(
        "\xD0\x90\xD0\xB2\xD1\x82\xD0\xBE\xD0\xBE\xD0\xB1\xD0\xBD\xD0\xBE\xD0\xB2\xD0\xBB\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB5"), this);
    intervalLbl->setStyleSheet(regionLbl->styleSheet());
    m_interval_ = new QComboBox(this);
    m_interval_->addItem(QString::fromUtf8("\xD0\x92\xD1\x8B\xD0\xBA\xD0\xBB"), 0);
    m_interval_->addItem(QStringLiteral("30 %1").arg(QString::fromUtf8("\xD1\x81\xD0\xB5\xD0\xBA")), 30);
    m_interval_->addItem(QStringLiteral("60 %1").arg(QString::fromUtf8("\xD1\x81\xD0\xB5\xD0\xBA")), 60);

    m_check_all_btn_ = new QPushButton(QString::fromUtf8(
        "\xD0\x9F\xD1\x80\xD0\xBE\xD0\xB2\xD0\xB5\xD1\x80\xD0\xB8\xD1\x82\xD1\x8C\x20\xD0\xB2\xD1\x81\xD0\xB5"), this);
    m_check_all_btn_->setObjectName("boostButton");
    m_check_sel_btn_ = new QPushButton(QString::fromUtf8(
        "\xD0\x9F\xD1\x80\xD0\xBE\xD0\xB2\xD0\xB5\xD1\x80\xD0\xB8\xD1\x82\xD1\x8C\x20\xD0\xB2\xD1\x8B\xD0\xB1\xD1\x80\xD0\xB0\xD0\xBD\xD0\xBD\xD1\x8B\xD0\xB9"), this);

    m_progress_ = new QLabel(QStringLiteral(" "), this);
    m_progress_->setStyleSheet(QString("color:%1; font-size:12px; background:transparent;")
                                   .arg(theme::Colors::TEXT_SECONDARY));

    m_details_ = new QTextEdit(this);
    m_details_->setReadOnly(true);
    m_details_->setStyleSheet(QString(
        "QTextEdit { background-color: %1; color: %2; border: 1px solid %7;"
        " border-radius: 8px; padding: 10px; font-size: 12px; }")
                                  .arg(theme::Colors::BG_SURFACE, theme::Colors::TEXT_PRIMARY,
                                       theme::Colors::BORDER));
    m_details_->setMinimumHeight(220);

    controls->addWidget(title);
    controls->addWidget(regionLbl);
    controls->addWidget(m_region_);
    controls->addWidget(m_labels_);
    controls->addWidget(m_grid_);
    controls->addWidget(intervalLbl);
    controls->addWidget(m_interval_);
    controls->addWidget(m_check_all_btn_);
    controls->addWidget(m_check_sel_btn_);
    controls->addWidget(m_progress_);
    controls->addWidget(m_details_, 1);

    root->addLayout(controls);

    // map canvas takes all remaining space; this widget IS the canvas
    setMouseTracking(false);
    setMinimumSize(640, 420);

    connect(m_region_, &QComboBox::currentIndexChanged, this, [this]() {
        saveSettings();
        rebuildVisibleServers();
        updateDetailsCard();
        update();
    });
    connect(m_labels_, &QCheckBox::toggled, this, [this]() { saveSettings(); update(); });
    connect(m_grid_, &QCheckBox::toggled, this, [this]() { saveSettings(); update(); });
    connect(m_interval_, &QComboBox::currentIndexChanged, this, [this]() {
        saveSettings();
        const int sec = m_interval_->currentData().toInt();
        if (sec > 0) m_timer_->start(sec * 1000);
        else m_timer_->stop();
    });
    connect(m_check_all_btn_, &QPushButton::clicked, this, &GeoMapWidget::onCheckAllClicked);
    connect(m_check_sel_btn_, &QPushButton::clicked, this, &GeoMapWidget::onCheckSelectedClicked);
}

void GeoMapWidget::loadSettings() {
    QSettings s;
    const int ri = m_region_->findData(s.value("map/region", "all").toString());
    if (ri >= 0) m_region_->setCurrentIndex(ri);
    m_labels_->setChecked(s.value("map/labels", true).toBool());
    m_grid_->setChecked(s.value("map/grid", true).toBool());
    const int sec = s.value("map/interval", 0).toInt();
    const int ii = std::max(0, m_interval_->findData(sec));
    m_interval_->setCurrentIndex(ii);
    if (sec > 0) { m_timer_->start(sec * 1000); }
}

void GeoMapWidget::saveSettings() {
    QSettings s;
    s.setValue("map/region", m_region_->currentData().toString());
    s.setValue("map/labels", m_labels_->isChecked());
    s.setValue("map/grid", m_grid_->isChecked());
    s.setValue("map/interval", m_interval_->currentData().toInt());
}

void GeoMapWidget::rebuildVisibleServers() {
    visible_.clear();
    const std::string region = m_region_->currentData().toString().toStdString();
    for (std::size_t i = 0; i < servers_.size(); ++i)
        if (ServerMapModel::regionOf(servers_[i].country) == region || region == "all")
            visible_.push_back(static_cast<int>(i));
}

QPointF GeoMapWidget::nodePos(const MapServer& s) const {
    const double w = static_cast<double>(width());
    const double h = static_cast<double>(height());
    return QPointF((s.longitude + 180.0) / 360.0 * w, (90.0 - s.latitude) / 180.0 * h);
}

int GeoMapWidget::pickNode(const QPoint& pos) const {
    int picked = -1;
    double best_dist = 14.0 * 14.0;
    for (int idx : visible_) {
        const QPointF p = nodePos(servers_[static_cast<std::size_t>(idx)]);
        const double dx = p.x() - pos.x();
        const double dy = p.y() - pos.y();
        const double d = dx * dx + dy * dy;
        if (d < best_dist) { best_dist = d; picked = idx; }
    }
    return picked;
}

void GeoMapWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    p.fillRect(rect(), QColor(theme::Colors::BG_PRIMARY));

    if (m_grid_->isChecked()) {
        p.setPen(QPen(QColor(0, 240, 255, 26), 1));
        for (int lon = -150; lon <= 150; lon += 30) {
            const double x = (lon + 180.0) / 360.0 * width();
            p.drawLine(QPointF(x, 0), QPointF(x, height()));
        }
        for (int lat = -60; lat <= 60; lat += 30) {
            const double y = (90.0 - lat) / 180.0 * height();
            p.drawLine(QPointF(0, y), QPointF(width(), y));
        }
    }

    for (int idx : visible_) {
        const MapServer& s = servers_[static_cast<std::size_t>(idx)];
        const QPointF pos = nodePos(s);
        bool neon = false;
        QColor c = gradeColor(s.latency_ms, &neon);
        if (neon && s.latency_ms >= 0) c = QColor(theme::Colors::SUCCESS); // measured but unknown-grade fallback

        const bool selected = (idx == selected_);
        const bool is_best = (idx == best_);

        if (selected || is_best) {
            QPen ring(c);
            ring.setWidth(2);
            p.setPen(ring);
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(pos, 11, 11);
        }

        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawEllipse(pos, selected ? 6 : 5, selected ? 6 : 5);

        p.setBrush(QColor(255, 255, 255, 180));
        p.drawEllipse(pos, 1.5, 1.5);

        if (m_labels_->isChecked()) {
            p.setPen(QColor(theme::Colors::TEXT_SECONDARY));
            QFont f = font();
            f.setPointSizeF(8.5);
            p.setFont(f);
            const QString label = QString::fromStdString(s.name) +
                                  (s.latency_ms >= 0
                                       ? QStringLiteral("  %1ms").arg(s.latency_ms)
                                       : QString());
            p.drawText(QPointF(pos.x() + 9, pos.y() + 3), label);
        }
    }
}

void GeoMapWidget::mousePressEvent(QMouseEvent* event) {
    const int idx = pickNode(event->pos());
    if (idx != -1) {
        selected_ = idx;
        updateDetailsCard();
        update();
    } else {
        selected_ = -1;
        updateDetailsCard();
        update();
    }
    QWidget::mousePressEvent(event);
}

void GeoMapWidget::updateDetailsCard() {
    if (selected_ < 0 || selected_ >= static_cast<int>(servers_.size())) {
        m_details_->setHtml(QString::fromUtf8(
            "<b><span style=\"color:#00F0FF\">\xD0\x9A\xD0\x90\xD0\xA0\xD0\xA2\xD0\x90</span></b><br>"
            "\xD0\x9A\xD0\xBB\xD0\xB8\xD0\xBA\xD0\xBD\xD0\xB8\xD1\x82\xD0\xB5 \xD0\xBF\xD0\xBE \xD1\x83\xD0\xB7\xD0\xBB\xD1\x83,"
            " \xD1\x87\xD1\x82\xD0\xBE\xD0\xB1\xD1\x8B \xD1\x83\xD0\xB2\xD0\xB8\xD0\xB4\xD0\xB5\xD1\x82\xD1\x8C \xD0\xB4\xD0\xB5\xD1\x82\xD0\xB0\xD0\xBB\xD0\xB8."));
        return;
    }

    const MapServer& s = servers_[static_cast<std::size_t>(selected_)];
    QString html;
    html += QString::fromUtf8("<b><span style=\"color:#00F0FF\">%1</span></b><br>")
                .arg(QString::fromStdString(s.name));
    html += QString::fromStdString(s.city + ", " + s.country) + "<br>";
    html += "IP: " + QString::fromStdString(s.ip) + "<br><br>";

    if (s.latency_ms >= 0) {
        html += gradeTag(s.latency_ms) + QStringLiteral(" <b>%1 ms</b><br>").arg(s.latency_ms);
        const auto g = ServerMapModel::grade(s.latency_ms);
        if (g == MapGrade::Good)
            html += QString::fromUtf8("\xD0\x9E\xD1\x82\xD0\xBB\xD0\xB8\xD1\x87\xD0\xBD\xD1\x8B\xD0\xB9 \xD1\x83\xD0\xB7\xD0\xB5\xD0\xBB \xD0\xB4\xD0\xBB\xD1\x8F \xD0\xB8\xD0\xB3\xD1\x80.");
        else if (g == MapGrade::Medium)
        html += QString::fromUtf8("\xD0\xA1\xD1\x80\xD0\xB5\xD0\xB4\xD0\xBD\xD0\xB8\xD0\xB9 \xD0\xBF\xD0\xB8\xD0\xBD\xD0\xB3 \x2D \xD0\xB8\xD0\xB3\xD1\x80\xD0\xB0\xD1\x82\xD1\x8C \xD0\xBC\xD0\xBE\xD0\xB6\xD0\xBD\xD0\xBE.");
        else
            html += QString::fromUtf8("\xD0\x92\xD1\x8B\xD1\x81\xD0\xBE\xD0\xBA\xD0\xB8\xD0\xB9 \xD0\xBF\xD0\xB8\xD0\xBD\xD0%B3.");
    } else {
        html += QString::fromUtf8("\xD0\x9D\xD0\xB5\x20\xD0\xBF\xD1\x80\xD0\xBE\xD0\xB2\xD0\xB5\xD1\x80\xD0\xB5\xD0\xBD");
    }

    if (selected_ == best_)
        html += QString::fromUtf8("<br><b><span style=\"color:%1\">BEST</span></b>").arg(theme::Colors::ACCENT_NEON);

    m_details_->setHtml(html);
}

void GeoMapWidget::startProbeThread(bool all) {
    m_check_all_btn_->setEnabled(false);
    m_check_sel_btn_->setEnabled(false);

    std::vector<int> targets;
    if (all) targets = visible_;
    else if (selected_ >= 0) targets.push_back(selected_);

    if (targets.empty()) {
        m_check_all_btn_->setEnabled(true);
        m_check_sel_btn_->setEnabled(true);
        emit probeProgress(QString::fromUtf8(
            "\xD0\x9D\xD0\xB5\xD1\x82\x20\xD0\xB2\xD1\x8B\xD0\xB1\xD1\x80\xD0\xB0\xD0\xBD\xD0\xBD\xD0\xBE\xD0\xB3\xD0\xBE\x20\xD1\x83\xD0\xB7\xD0\xBB\xD0\xB0"));
        return;
    }

    std::thread([this, targets]() {
        static std::atomic<bool> busy{false};
        bool expected = false;
        if (!busy.compare_exchange_strong(expected, true)) return;

        SpeedTest speedtest;
        int done = 0;
        for (int idx : targets) {
            auto& s = servers_[static_cast<std::size_t>(idx)];
            const auto res = speedtest.benchmarkServer(s.ip);
            s.latency_ms = res.success ? static_cast<int>(std::lround(res.latency_ms)) : -2;
            ++done;
            emit probeProgress(QStringLiteral("%1 / %2").arg(done).arg(targets.size()));
            update();
        }

        busy = false;
        emit probeFinished();
        QMetaObject::invokeMethod(this, [this]() {
            m_check_all_btn_->setEnabled(true);
            m_check_sel_btn_->setEnabled(true);
        }, Qt::QueuedConnection);
    }).detach();
}

void GeoMapWidget::onCheckAllClicked() { startProbeThread(true); }
void GeoMapWidget::onCheckSelectedClicked() { startProbeThread(false); }

} // namespace gno
