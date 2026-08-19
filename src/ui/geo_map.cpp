#include "geo_map.h"
#include "theme.h"
#include <QPainter>
#include <QPainterPath>

namespace gno {

GeoMapWidget::GeoMapWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(300);
    setMinimumSize(600, 300);
    initMapData();
}

void GeoMapWidget::initMapData()
{
    points_ = {
        {0.48, 0.32, "Moscow",     QColor(theme::Colors::ACCENT_BLUE)},
        {0.47, 0.30, "St Petersburg", QColor(theme::Colors::ACCENT_CYAN)},
        {0.46, 0.28, "Helsinki",   QColor(theme::Colors::TEXT_SECONDARY)},
        {0.44, 0.27, "Stockholm",  QColor(theme::Colors::TEXT_SECONDARY)},
        {0.45, 0.29, "Warsaw",     QColor(theme::Colors::TEXT_SECONDARY)},
        {0.45, 0.30, "Berlin",     QColor(theme::Colors::ACCENT_CYAN)},
        {0.44, 0.30, "Frankfurt",  QColor(theme::Colors::SUCCESS)},
        {0.43, 0.30, "Amsterdam",  QColor(theme::Colors::SUCCESS)},
        {0.42, 0.29, "London",     QColor(theme::Colors::SUCCESS)},
        {0.40, 0.28, "New York",   QColor(theme::Colors::ACCENT_BLUE)},
        {0.37, 0.30, "Los Angeles", QColor(theme::Colors::ACCENT_VIOLET)},
        {0.53, 0.30, "Tokyo",      QColor(theme::Colors::WARNING)},
        {0.52, 0.32, "Singapore",  QColor(theme::Colors::ACCENT_CYAN)},
        {0.56, 0.37, "Sydney",     QColor(theme::Colors::WARNING)},
        {0.38, 0.36, "São Paulo",  QColor(theme::Colors::ERROR)},
    };

    routes_ = {
        {0, 7, QColor(theme::Colors::ACCENT_BLUE)},
        {0, 8, QColor(theme::Colors::ACCENT_BLUE)},
        {0, 11, QColor(theme::Colors::ACCENT_VIOLET)},
        {0, 12, QColor(theme::Colors::ACCENT_CYAN)},
        {7, 9, QColor(theme::Colors::SUCCESS)},
        {7, 10, QColor(theme::Colors::SUCCESS)},
        {11, 12, QColor(theme::Colors::WARNING)},
    };
}

void GeoMapWidget::setHighlightedRoute(int index)
{
    highlight_ = index;
    update();
}

void GeoMapWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    p.fillRect(rect(), QColor(theme::Colors::BG_PRIMARY));

    QPen gridPen(QColor(theme::Colors::BORDER));
    gridPen.setWidthF(0.5);
    p.setPen(gridPen);

    int w = width();
    int h = height();

    for (int x = 0; x < w; x += 40) {
        p.drawLine(x, 0, x, h);
    }
    for (int y = 0; y < h; y += 40) {
        p.drawLine(0, y, w, y);
    }

    for (size_t i = 0; i < routes_.size(); ++i) {
        const auto& r = routes_[i];
        if (r.from >= static_cast<int>(points_.size()) || r.to >= static_cast<int>(points_.size())) continue;

        const auto& p1 = points_[r.from];
        const auto& p2 = points_[r.to];

        QPen routePen(r.color);
        routePen.setWidthF((static_cast<int>(i) == highlight_) ? 3.0 : 1.5);
        if (static_cast<int>(i) != highlight_) {
            routePen.setStyle(Qt::DashLine);
        }
        p.setPen(routePen);
        p.setBrush(Qt::NoBrush);

        QPainterPath path;
        double cx = (p1.x + p2.x) / 2.0;
        double cy = (std::min(p1.y, p2.y)) - 0.03;
        path.moveTo(p1.x * w, p1.y * h);
        path.quadTo(cx * w, cy * h, p2.x * w, p2.y * h);
        p.drawPath(path);
    }

    for (const auto& pt : points_) {
        int px = static_cast<int>(pt.x * w);
        int py = static_cast<int>(pt.y * h);

        p.setPen(Qt::NoPen);
        p.setBrush(pt.color);
        p.drawEllipse(QPoint(px, py), 6, 6);

        p.setPen(QColor(theme::Colors::BG_PRIMARY));
        p.setBrush(pt.color);
        p.drawEllipse(QPoint(px, py), 3, 3);

        p.setPen(pt.color);
        QFont f("Segoe UI", 8);
        p.setFont(f);
        p.drawText(px + 10, py + 4, pt.label);
    }
}

} // namespace gno
