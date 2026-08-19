#pragma once

#include <QWidget>
#include <QPainter>
#include <QPainterPath>
#include <vector>

namespace gno {

struct GeoPoint {
    double x = 0.0;
    double y = 0.0;
    QString label;
    QColor color;
};

struct GeoRoute {
    int from = 0;
    int to = 0;
    QColor color;
};

class GeoMapWidget : public QWidget {
    Q_OBJECT

public:
    explicit GeoMapWidget(QWidget* parent = nullptr);

    void setHighlightedRoute(int index);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void initMapData();

    std::vector<GeoPoint> points_;
    std::vector<GeoRoute> routes_;
    int highlight_ = -1;
};

} // namespace gno
