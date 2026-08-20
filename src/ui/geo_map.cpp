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
        {0.48, 0.32, "Москва",      QColor(theme::Colors::ACCENT_BLUE)},
        {0.47, 0.30, "Санкт-Петербург", QColor(theme::Colors::ACCENT_CYAN)},
        {0.46, 0.28, "Хельсинки",   QColor(theme::Colors::TEXT_SECONDARY)},
        {0.44, 0.27, "Стокгольм",   QColor(theme::Colors::TEXT_SECONDARY)},
        {0.45, 0.29, "Варшава",     QColor(theme::Colors::TEXT_SECONDARY)},
        {0.45, 0.30, "Берлин",      QColor(theme::Colors::ACCENT_CYAN)},
        {0.44, 0.30, "Франкфурт",   QColor(theme::Colors::SUCCESS)},
        {0.43, 0.30, "Амстердам",   QColor(theme::Colors::SUCCESS)},
        {0.42, 0.29, "Лондон",      QColor(theme::Colors::SUCCESS)},
        {0.40, 0.28, "Нью-Йорк",    QColor(theme::Colors::ACCENT_BLUE)},
        {0.37, 0.30, "Лос-Анджелес", QColor(theme::Colors::ACCENT_VIOLET)},
        {0.53, 0.30, "Токио",       QColor(theme::Colors::WARNING)},
        {0.52, 0.32, "Сингапур",    QColor(theme::Colors::ACCENT_CYAN)},
        {0.56, 0.37, "Сидней",      QColor(theme::Colors::WARNING)},
        {0.38, 0.36, "Сан-Паулу",   QColor(theme::Colors::ERROR)},
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

    // header
    QFont titleFont("Segoe UI", 12, QFont::Bold);
    p.setFont(titleFont);
    p.setPen(QColor(255, 255, 255, 220));
    p.drawText(24, 30, QString::fromUtf8("Карта серверов"));

    QFont subFont("Segoe UI", 9);
    p.setFont(subFont);
    p.setPen(QColor(255, 255, 255, 90));
    p.drawText(24, 48, QString::fromUtf8("Маршруты и серверы для игрового трафика"));

    QRect mapArea(rect().adjusted(12, 60, -12, -12));

    QPen gridPen(QColor(theme::Colors::BORDER));
    gridPen.setWidthF(0.5);
    p.setPen(gridPen);

    int w = mapArea.width();
    int h = mapArea.height();

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
        path.moveTo(mapArea.left() + p1.x * w, mapArea.top() + p1.y * h);
        path.quadTo(mapArea.left() + cx * w, mapArea.top() + cy * h, mapArea.left() + p2.x * w, mapArea.top() + p2.y * h);
        p.drawPath(path);
    }

    for (const auto& pt : points_) {
        int px = mapArea.left() + static_cast<int>(pt.x * w);
        int py = mapArea.top() + static_cast<int>(pt.y * h);

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
