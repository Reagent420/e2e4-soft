#include "geo_map.h"
#include "theme.h"
#include "core/speed_test.h"
#include "core/server_map_model.h"
#include <QTextEdit>

#include <QApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QSettings>
#include <QTime>
#include <QVBoxLayout>

#include <atomic>
#include <cmath>
#include <thread>

namespace gno {

namespace {

QColor gradeColor(int latency) {
    switch (ServerMapModel::grade(latency)) {
        case MapGrade::Good:    return QColor(theme::Colors::SUCCESS);
        case MapGrade::Medium:  return QColor(theme::Colors::WARNING);
        case MapGrade::Bad:     return QColor(0xFF, 0x2E, 0x88);
        case MapGrade::Offline: return QColor(0x8A, 0x4A, 0x6E);
        default:                break;
    }
    return QColor(theme::Colors::ACCENT_NEON);
}

QString gradeTag(int latency) {
    switch (ServerMapModel::grade(latency)) {
        case MapGrade::Good:    return QStringLiteral("[OK]");
        case MapGrade::Medium:  return QStringLiteral("[MED]");
        case MapGrade::Bad:     return QStringLiteral("[BAD]");
        case MapGrade::Offline: return QStringLiteral("[OFF]");
        default:                return QStringLiteral("[?]");
    }
}

// Rough continental outlines (lon, lat), stylized wireframe quality.
struct WorldPoly { const double* pts; int n; };

const double kNorthAmerica[] = {
    -168,66, -140,70, -125,71, -110,72, -95,72, -80,73, -70,62, -55,52,
    -65,45, -75,40, -81,31, -80,26, -97,26, -105,22, -114,29, -124,40,
    -130,55, -145,60, -160,58};
const double kSouthAmerica[] = {
    -81,8, -75,10, -62,10, -50,0, -35,-8, -39,-15, -48,-28, -58,-34,
    -62,-41, -65,-50, -70,-54, -75,-46, -72,-30, -70,-18, -77,-6, -81,2};
const double kGreenland[] = {
    -58,76, -40,83, -20,80, -25,70, -42,60, -52,64};
const double kEurasia[] = {
    -9,43, -1,49, 5,53, 10,57, 20,55, 30,59, 45,66, 70,72, 100,76,
    140,72, 160,69, 180,66, 178,62, 160,60, 155,53, 142,46, 135,43,
    127,40, 122,30, 112,22, 104,10, 98,8, 92,20, 88,21, 80,10, 72,20,
    66,25, 58,26, 50,29, 44,37, 36,36, 27,40, 22,38, 14,38, 8,44, 0,47, -4,44};
const double kAfrica[] = {
    -17,15, -8,32, 0,36, 11,33, 20,32, 32,31, 34,27, 38,18, 43,11,
    51,12, 45,0, 40,-8, 35,-18, 32,-26, 26,-34, 19,-34, 14,-26,
    12,-18, 8,-2, -5,4, -12,8};
const double kAustralia[] = {
    113,-22, 122,-17, 131,-11, 137,-12, 143,-13, 147,-19, 153,-26,
    150,-34, 144,-38, 138,-35, 129,-32, 118,-34, 114,-28};

const WorldPoly kWorld[] = {
    {kNorthAmerica, sizeof(kNorthAmerica)/sizeof(double)/2},
    {kSouthAmerica, sizeof(kSouthAmerica)/sizeof(double)/2},
    {kGreenland,    sizeof(kGreenland)/sizeof(double)/2},
    {kEurasia,      sizeof(kEurasia)/sizeof(double)/2},
    {kAfrica,       sizeof(kAfrica)/sizeof(double)/2},
    {kAustralia,    sizeof(kAustralia)/sizeof(double)/2},
};

} // namespace

// ================================================================== canvas

MapCanvas::MapCanvas(QWidget* parent) : QWidget(parent) {
    setMinimumSize(420, 360);
    setCursor(Qt::PointingHandCursor);
}

void MapCanvas::bind(std::vector<MapServer>* servers, std::vector<int>* visible,
                     int* selected, int* best,
                     const bool* show_labels, const bool* show_grid) {
    servers_ = servers; visible_ = visible; selected_ = selected; best_ = best;
    show_labels_ = show_labels; show_grid_ = show_grid;
}

void MapCanvas::setPulsePhase(bool on) {
    pulse_on_ = on;
    update();
}

QPointF MapCanvas::project(double lon, double lat) const {
    const double w = static_cast<double>(width());
    const double h = static_cast<double>(height());
    const double mx = 16, my = 16;
    return QPointF(mx + (lon + 180.0) / 360.0 * (w - 2 * mx),
                   my + (90.0 - lat) / 180.0 * (h - 2 * my));
}

int MapCanvas::pickNode(const QPoint& pos) const {
    int picked = -1;
    double best_d2 = 16.0 * 16.0;
    for (int idx : *visible_) {
        const auto& s = (*servers_)[static_cast<std::size_t>(idx)];
        const QPointF p = project(s.longitude, s.latitude);
        const double dx = p.x() - pos.x(), dy = p.y() - pos.y();
        if (dx * dx + dy * dy < best_d2) { best_d2 = dx * dx + dy * dy; picked = idx; }
    }
    return picked;
}

void MapCanvas::resizeEvent(QResizeEvent*) { update(); }

void MapCanvas::mousePressEvent(QMouseEvent* e) {
    if (selected_) {
        *selected_ = pickNode(e->pos());
        if (onClicked) onClicked();
        update();
    }
    QWidget::mousePressEvent(e);
}

void MapCanvas::drawWorld(QPainter& p) const {
    QPen pen(QColor(0, 240, 255, 80));
    pen.setWidthF(1.2);
    p.setPen(pen);
    p.setBrush(QColor(0, 240, 255, 12));

    for (const auto& poly : kWorld) {
        QPainterPath path;
        for (int i = 0; i < poly.n; ++i) {
            const QPointF pt = project(poly.pts[i * 2], poly.pts[i * 2 + 1]);
            if (i == 0) path.moveTo(pt); else path.lineTo(pt);
        }
        path.closeSubpath();
        p.drawPath(path);
    }
}

void MapCanvas::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // deep background with subtle vertical gradient
    QLinearGradient bg(0, 0, 0, height());
    bg.setColorAt(0, QColor(0x0A, 0x0E, 0x17));
    bg.setColorAt(1, QColor(0x08, 0x10, 0x1C));
    p.fillRect(rect(), bg);

    if (show_grid_ && *show_grid_) {
        p.setPen(QPen(QColor(0, 240, 255, 18), 1));
        for (int lon = -150; lon <= 150; lon += 30) {
            const QPointF a = project(lon, 90), b = project(lon, -90);
            p.drawLine(a, b);
        }
        for (int lat = -60; lat <= 60; lat += 30) {
            const QPointF a = project(-180, lat), b = project(180, lat);
            p.drawLine(a, b);
        }
    }

    drawWorld(p);

    if (!servers_ || !visible_) return;

    QFont f = font();
    f.setPointSizeF(8.5);
    p.setFont(f);

    for (int idx : *visible_) {
        const MapServer& s = (*servers_)[static_cast<std::size_t>(idx)];
        const QPointF pos = project(s.longitude, s.latitude);
        const QColor c = gradeColor(s.latency_ms);
        const MapGrade g = ServerMapModel::grade(s.latency_ms);
        const bool selected = (idx == *selected_);
        const bool is_best = (idx == *best_);

        if (g == MapGrade::Unknown) {
            QPen pen(QColor(theme::Colors::ACCENT_NEON));
            pen.setWidth(2);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(pos, 6, 6);
        } else {
            QColor halo = c;
            halo.setAlpha(selected ? 90 : 60);
            p.setPen(Qt::NoPen);
            p.setBrush(halo);
            const qreal halo_r = (selected && pulse_on_) ? 13 : 10;
            p.drawEllipse(pos, halo_r, halo_r);

            p.setBrush(c);
            p.drawEllipse(pos, selected ? 7 : 5, selected ? 7 : 5);

            if (g == MapGrade::Offline) {
                QPen xp(QColor(255, 255, 255, 140), 1.6);
                p.setPen(xp);
                p.drawLine(QPointF(pos.x() - 3, pos.y() - 3), QPointF(pos.x() + 3, pos.y() + 3));
                p.drawLine(QPointF(pos.x() - 3, pos.y() + 3), QPointF(pos.x() + 3, pos.y() - 3));
            }
        }

        if (selected || is_best) {
            QPen ring(is_best ? QColor(theme::Colors::ACCENT_NEON) : QColor(Qt::white));
            ring.setWidth(2);
            p.setPen(ring);
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(pos, 13, 13);
        }

        if (show_labels_ && *show_labels_) {
            const QString label = QString::fromStdString(s.name) +
                                  (s.latency_ms >= 0
                                       ? QStringLiteral("  %1ms").arg(s.latency_ms)
                                       : QString());
            const QFontMetrics fm(f);
            const int tw = fm.horizontalAdvance(label);
            // stagger labels to reduce overlap in dense regions
            const bool above = (idx % 2) == 0;
            QRect pill(static_cast<int>(pos.x()) + 10,
                       static_cast<int>(pos.y()) + (above ? -24 : 6),
                       tw + 10, 17);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(6, 14, 26, 205));
            p.drawRoundedRect(pill, 4, 4);
            p.setPen(QColor(theme::Colors::TEXT_PRIMARY));
            p.drawText(pill, Qt::AlignVCenter | Qt::AlignHCenter, label);
        }
    }
}

// ================================================================== page

GeoMapWidget::GeoMapWidget(QWidget* parent) : QWidget(parent) {
    SpeedTest speedtest;
    for (const auto& n : speedtest.getServers())
        servers_.push_back(MapServer{n.name, n.city, n.country, n.ip,
                                     n.latitude, n.longitude, -1});

    setupUI();
    loadSettings();
    rebuildVisibleServers();
    canvas_->bind(&servers_, &visible_, &selected_, &best_,
                  &m_labels_shown_, &m_grid_shown_);
    updateDetailsCard();

    m_timer_ = new QTimer(this);
    connect(m_timer_, &QTimer::timeout, this, [this]() {
        if (isVisible()) onCheckAllClicked();
    });

    m_pulse_timer_ = new QTimer(this);
    m_pulse_timer_->setInterval(600);
    connect(m_pulse_timer_, &QTimer::timeout, this, [this]() {
        pulse_phase_ = !pulse_phase_;
        canvas_->setPulsePhase(pulse_phase_);
    });
    m_pulse_timer_->start(600);

    canvas_->onClicked = [this]() { updateDetailsCard(); };
}

void GeoMapWidget::showEvent(QShowEvent* event) {
    if (!first_probe_done_) {
        first_probe_done_ = true;
        QTimer::singleShot(300, this, [this]() { onCheckAllClicked(); });
    }
    QWidget::showEvent(event);
}

void GeoMapWidget::setupUI() {
    setObjectName("geoMapPage");
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(14);

    canvas_ = new MapCanvas(this);
    root->addWidget(canvas_, /*stretch*/ 1);

    auto* panelWrap = new QWidget(this);
    panelWrap->setFixedWidth(320);
    panelWrap->setObjectName("settingsGroup");
    auto* panel = new QVBoxLayout(panelWrap);
    panel->setContentsMargins(16, 14, 16, 14);
    panel->setSpacing(8);

    auto* title = new QLabel(QStringLiteral("SERVER MAP"), panelWrap);
    title->setStyleSheet(QString("font-size:17px; font-weight:700; letter-spacing:3px;"
                                 "color:%1; background:transparent;")
                             .arg(theme::Colors::ACCENT_NEON));

    auto* regionLbl = new QLabel(QStringLiteral("REGION"), panelWrap);
    regionLbl->setStyleSheet(QString("color:%1; font-size:11px; background:transparent;")
                                 .arg(theme::Colors::TEXT_SECONDARY));
    m_region_ = new QComboBox(panelWrap);
    m_region_->addItem(QStringLiteral("ALL"), "all");
    m_region_->addItem("EU", "EU");
    m_region_->addItem("NA", "NA");
    m_region_->addItem("ASIA", "ASIA");
    m_region_->addItem("SA", "SA");
    m_region_->addItem("OCE", "OCE");

    m_labels_ = new QCheckBox(QStringLiteral("Labels"), panelWrap);
    m_grid_ = new QCheckBox(QStringLiteral("Grid"), panelWrap);
    m_labels_->setChecked(true);
    m_grid_->setChecked(true);

    auto* intervalLbl = new QLabel(QStringLiteral("AUTO REFRESH"), panelWrap);
    intervalLbl->setStyleSheet(regionLbl->styleSheet());
    m_interval_ = new QComboBox(panelWrap);
    m_interval_->addItem(QStringLiteral("OFF"), 0);
    m_interval_->addItem(QStringLiteral("30 s"), 30);
    m_interval_->addItem(QStringLiteral("60 s"), 60);

    m_check_all_btn_ = new QPushButton(QStringLiteral("PROBE ALL"), panelWrap);
    m_check_all_btn_->setObjectName("boostButton");
    m_check_sel_btn_ = new QPushButton(QStringLiteral("PROBE SELECTED"), panelWrap);

    m_progress_ = new QLabel(QStringLiteral(" "), panelWrap);
    m_progress_->setStyleSheet(QString("color:%1; font-size:12px; background:transparent;")
                                   .arg(theme::Colors::TEXT_SECONDARY));

    m_details_ = new QTextEdit(panelWrap);
    m_details_->setReadOnly(true);
    m_details_->setStyleSheet(QString(
        "QTextEdit { background-color: %1; color: %2; border: 1px solid %7;"
        " border-radius: 8px; padding: 10px; font-size: 12px; }")
                                  .arg(theme::Colors::BG_SURFACE, theme::Colors::TEXT_PRIMARY,
                                       theme::Colors::BORDER));

    panel->addWidget(title);
    panel->addWidget(regionLbl);
    panel->addWidget(m_region_);
    panel->addWidget(m_labels_);
    panel->addWidget(m_grid_);
    panel->addWidget(intervalLbl);
    panel->addWidget(m_interval_);
    panel->addWidget(m_check_all_btn_);
    panel->addWidget(m_check_sel_btn_);
    panel->addWidget(m_progress_);
    panel->addWidget(m_details_, 1);

    root->addWidget(panelWrap);

    connect(m_region_, &QComboBox::currentIndexChanged, this, [this]() {
        saveSettings();
        rebuildVisibleServers();
        updateDetailsCard();
        canvas_->update();
    });
    connect(m_labels_, &QCheckBox::toggled, this, [this]() {
        m_labels_shown_ = m_labels_->isChecked();
        saveSettings();
        canvas_->update();
    });
    connect(m_grid_, &QCheckBox::toggled, this, [this]() {
        m_grid_shown_ = m_grid_->isChecked();
        saveSettings();
        canvas_->update();
    });
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
    m_labels_shown_ = m_labels_->isChecked();
    m_grid_shown_ = m_grid_->isChecked();
    const int sec = s.value("map/interval", 0).toInt();
    const int ii = std::max(0, m_interval_->findData(sec));
    m_interval_->setCurrentIndex(ii);
    if (sec > 0) m_timer_->start(sec * 1000);
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
    const std::string region = m_region_ ? m_region_->currentData().toString().toStdString() : "all";
    for (std::size_t i = 0; i < servers_.size(); ++i)
        if (region == "all" || ServerMapModel::regionOf(servers_[i].country) == region)
            visible_.push_back(static_cast<int>(i));
}

void GeoMapWidget::updateDetailsCard() {
    if (selected_ < 0 || selected_ >= static_cast<int>(servers_.size())) {
        m_details_->setHtml(QString::fromUtf8(
            "<b><span style=\"color:#00F0FF\">SERVER MAP</span></b><br>"
            "\xD0\x9A\xD0\xBB\xD0\xB8\xD0\xBA\xD0\xBD\xD0\xB8\xD1\x82\xD0\xB5\x20\xD0\xBF\xD0\xBE\x20\xD1\x83\xD0\xB7\xD0\xBB\xD1\x83."));
        return;
    }
    const MapServer& s = servers_[static_cast<std::size_t>(selected_)];
    QString html;
    html += QString::fromUtf8("<b><span style=\"color:#00F0FF\">%1</span></b><br>")
                .arg(QString::fromStdString(s.name));
    html += QString::fromStdString(s.city + ", " + s.country) + "<br>";
    html += "IP: " + QString::fromStdString(s.ip) + "<br><br>";

    html += gradeTag(s.latency_ms) + " ";
    if (s.latency_ms >= 0) html += QStringLiteral("<b>%1 ms</b><br>").arg(s.latency_ms);
    else if (s.latency_ms == -2)
        html += QString::fromUtf8(
            "\xD0\x9D\xD0\xB5\xD0%B4\xD0%BE\xD1\x81\xD1%82\xD1%83\xD0\xBF\xD0%B5\xD0\xBD<br>");
    else
        html += QString::fromUtf8("\xD0\x9D\xD0\xB5\x20\xD0\xBF\xD1%80\xD0\xBE\xD0%B2%D0%B5%D1%80\xD0%B5\xD0\xBD<br>");

    if (selected_ == best_)
        html += QString::fromUtf8("<br><b><span style=\"color:%1\">BEST</span></b>")
                    .arg(theme::Colors::ACCENT_NEON);
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
        return;
    }

    std::thread([this, targets]() {
        static std::atomic<bool> busy{false};
        bool expected = false;
        if (!busy.compare_exchange_strong(expected, true)) return;

        SpeedTest speedtest;
        std::vector<std::pair<int, int>> results;
        int done = 0;
        try {
            for (int idx : targets) {
                const auto& s = servers_[static_cast<std::size_t>(idx)];
                const auto res = speedtest.benchmarkServer(s.ip, /*probes*/ 3);
                results.emplace_back(idx, res.success
                                             ? static_cast<int>(std::lround(res.latency_ms))
                                             : -2);
                ++done;
                emit probeProgress(QStringLiteral("%1 / %2").arg(done).arg(targets.size()));
            }
        } catch (...) {}
        busy = false;

        QMetaObject::invokeMethod(this, [this, results]() {
            ServerMapModel::applyProbeResults(servers_, results);
            best_ = ServerMapModel::bestServer(servers_);
            updateDetailsCard();
            canvas_->update();
            m_check_all_btn_->setEnabled(true);
            m_check_sel_btn_->setEnabled(true);
        }, Qt::QueuedConnection);
    }).detach();
}

void GeoMapWidget::onCheckAllClicked() { startProbeThread(true); }
void GeoMapWidget::onCheckSelectedClicked() { startProbeThread(false); }

} // namespace gno
