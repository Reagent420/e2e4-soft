#include "report_export.h"

#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QDateTime>
#include <QFont>
#include <QWidget>
#include <algorithm>
#include <cmath>

#include "theme.h"

namespace gno {
namespace report {

namespace {

constexpr int kPageWidth = 1240;
constexpr int kMargin = 48;
constexpr int kHeaderH = 120;

QColor textPrimary()    { return QColor(theme::Colors::TEXT_PRIMARY); }
QColor textSecondary()  { return QColor(theme::Colors::TEXT_SECONDARY); }
QColor accent()         { return QColor(theme::Colors::ACCENT_BLUE); }
QColor accentCyan()     { return QColor(theme::Colors::ACCENT_CYAN); }
QColor success()        { return QColor(theme::Colors::SUCCESS); }
QColor warning()        { return QColor(theme::Colors::WARNING); }
QColor error()          { return QColor(theme::Colors::ERROR); }
QColor bgSurface()      { return QColor(theme::Colors::BG_SURFACE); }
QColor bgPrimary()      { return QColor(theme::Colors::BG_PRIMARY); }

void drawHeader(QPainter& p, const QString& subtitle) {
    QLinearGradient grad(0, 0, kPageWidth, 0);
    grad.setColorAt(0.0, QColor(26, 26, 46));
    grad.setColorAt(1.0, QColor(37, 37, 64));
    p.fillRect(QRect(0, 0, kPageWidth, kHeaderH), grad);

    p.setPen(QPen(QColor(59, 130, 246, 140), 1));
    p.drawLine(0, kHeaderH - 1, kPageWidth, kHeaderH - 1);

    QFont big("Segoe UI", 22, QFont::Bold);
    p.setFont(big);
    p.setPen(textPrimary());
    p.drawText(QRect(kMargin, 24, 700, 40), Qt::AlignLeft | Qt::AlignVCenter,
               QString::fromUtf8("E2E4 Soft — отчёт"));

    QFont sub("Segoe UI", 12);
    p.setFont(sub);
    p.setPen(textSecondary());
    p.drawText(QRect(kMargin, 66, 700, 30), Qt::AlignLeft | Qt::AlignVCenter, subtitle);

    QFont date("Segoe UI", 12);
    p.setFont(date);
    p.setPen(textSecondary());
    p.drawText(QRect(kPageWidth - 380, 24, 332, 30), Qt::AlignRight | Qt::AlignVCenter,
               QDateTime::currentDateTime().toString("dd.MM.yyyy HH:mm"));

    p.drawText(QRect(kPageWidth - 380, 66, 332, 30), Qt::AlignRight | Qt::AlignVCenter,
               QString::fromUtf8("v%1 · %2").arg(theme::APP_VERSION, theme::APP_NAME));
}

void drawSectionTitle(QPainter& p, int y, const QString& text) {
    QFont f("Segoe UI", 13, QFont::Bold);
    p.setFont(f);
    p.setPen(accentCyan());
    p.drawText(QRect(kMargin, y, 800, 30), Qt::AlignLeft | Qt::AlignVCenter, text);
}

void drawLineChart(QPainter& p, const QRect& r, const QVector<double>& data,
                   double maxVal, const QColor& lineColor, const QColor& fillColor) {
    p.fillRect(r, bgSurface());
    p.setPen(QPen(QColor(30, 41, 59), 1));
    for (int i = 1; i <= 3; ++i) {
        int y = r.top() + r.height() * i / 4;
        p.drawLine(r.left(), y, r.right(), y);
    }

    if (data.size() < 2) return;
    int n = data.size();
    double step = r.width() / static_cast<double>(n - 1);

    QPainterPath path;
    QPolygonF poly;
    for (int i = 0; i < n; ++i) {
        double v = qBound(0.0, data[i], maxVal);
        double x = r.left() + i * step;
        double y = r.bottom() - v / maxVal * r.height();
        if (i == 0) path.moveTo(x, y);
        else path.lineTo(x, y);
        poly << QPointF(x, y);
    }
    path.lineTo(r.right(), r.bottom());
    path.lineTo(r.left(), r.bottom());
    path.closeSubpath();

    QLinearGradient grad(r.topLeft(), r.bottomLeft());
    grad.setColorAt(0.0, fillColor);
    grad.setColorAt(1.0, QColor(fillColor.red(), fillColor.green(), fillColor.blue(), 5));
    p.fillPath(path, grad);

    p.setPen(QPen(lineColor, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolyline(poly);
}

void drawStatCard(QPainter& p, int x, int y, int w, int h, const QString& label,
                  const QString& value, const QColor& valueColor) {
    QRect r(x, y, w, h);
    p.fillRect(r, bgSurface());
    p.setPen(QPen(QColor(255, 255, 255, 20), 1));
    p.drawRect(r);

    QFont lf("Segoe UI", 11);
    p.setFont(lf);
    p.setPen(textSecondary());
    p.drawText(r.adjusted(14, 10, -14, -14), Qt::AlignTop | Qt::AlignLeft, label);

    QFont vf("Segoe UI", 22, QFont::Bold);
    p.setFont(vf);
    p.setPen(valueColor);
    p.drawText(r.adjusted(14, 34, -14, -8), Qt::AlignLeft | Qt::AlignVCenter, value);
}

QString formatImprovement(double before, double after, bool lowerIsBetter) {
    if (before <= 0.0) return QString();
    double diff = after - before;
    if (lowerIsBetter)
        diff = before - after;
    if (std::fabs(diff) < 0.05) return QString::fromUtf8("без изменений");
    double pct = diff / before * 100.0;
    bool good = lowerIsBetter ? (after < before) : (after > before);
    return QString("%1%2 (%3%)")
        .arg(good ? "+" : "")
        .arg(diff, 0, 'f', 1)
        .arg(pct, 0, 'f', 1);
}

} // namespace

QString defaultReportsDir() {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (dir.isEmpty())
        dir = QDir::homePath();
    dir += "/E2E4 Soft Reports";
    QDir().mkpath(dir);
    return dir;
}

bool exportWidgetScreenshot(QWidget* widget, const QString& path) {
    if (!widget) return false;
    QPixmap pm = widget->grab();
    return pm.save(path, "PNG");
}

bool exportMonitoringReport(const QString& path,
                            const QVector<double>& ping,
                            const QVector<double>& jitter,
                            const QVector<double>& loss,
                            double avgPing, double avgJitter, double avgLoss,
                            const QStringList& recommendations) {
    QImage img(kPageWidth, 1600, QImage::Format_RGB32);
    img.fill(bgPrimary());

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);

    drawHeader(p, QString::fromUtf8("Мониторинг сети — реальные замеры к 1.1.1.1"));

    int y = kHeaderH + 32;

    // stat cards
    const int cw = (kPageWidth - 2 * kMargin - 30) / 4;
    const int ch = 110;
    drawStatCard(p, kMargin, y, cw, ch, QString::fromUtf8("СРЕДНИЙ ПИНГ"), QString("%1 мс").arg(avgPing, 0, 'f', 1), accent());
    drawStatCard(p, kMargin + cw + 10, y, cw, ch, QString::fromUtf8("СРЕДНИЙ ДЖИТТЕР"), QString("%1 мс").arg(avgJitter, 0, 'f', 1), accentCyan());
    drawStatCard(p, kMargin + 2 * (cw + 10), y, cw, ch, QString::fromUtf8("ПОТЕРИ ПАКЕТОВ"), QString("%1%").arg(avgLoss, 0, 'f', 2),
                 avgLoss < 0.5 ? success() : avgLoss < 2.0 ? warning() : error());
    drawStatCard(p, kMargin + 3 * (cw + 10), y, cw, ch, QString::fromUtf8("ЗАМЕРОВ"), QString::number(ping.size()), textPrimary());
    y += ch + 30;

    // charts
    drawSectionTitle(p, y, QString::fromUtf8("Пинг (мс)"));
    y += 38;
    drawLineChart(p, QRect(kMargin, y, kPageWidth - 2 * kMargin, 170), ping, 120.0, accent(), QColor(59, 130, 246, 70));
    y += 190;

    drawSectionTitle(p, y, QString::fromUtf8("Джиттер (мс)"));
    y += 38;
    drawLineChart(p, QRect(kMargin, y, kPageWidth - 2 * kMargin, 170), jitter, 20.0, accentCyan(), QColor(6, 182, 212, 70));
    y += 190;

    drawSectionTitle(p, y, QString::fromUtf8("Потери пакетов (%)"));
    y += 38;
    drawLineChart(p, QRect(kMargin, y, kPageWidth - 2 * kMargin, 170), loss, 10.0, error(), QColor(239, 68, 68, 70));
    y += 190;

    // recommendations
    drawSectionTitle(p, y, QString::fromUtf8("Рекомендации"));
    y += 36;
    QFont rf("Segoe UI", 12);
    p.setFont(rf);
    int i = 0;
    for (const QString& rec : recommendations) {
        p.setPen(textSecondary());
        p.drawText(QRect(kMargin, y + i * 26, 60, 24), Qt::AlignLeft | Qt::AlignVCenter,
                   QString::number(i + 1) + ".");
        p.setPen(textPrimary());
        p.drawText(QRect(kMargin + 44, y + i * 26, kPageWidth - 2 * kMargin - 44, 24),
                   Qt::AlignLeft | Qt::AlignVCenter, rec);
        ++i;
        if (y + i * 26 > 1500) break;
    }

    p.end();
    return img.save(path, "PNG");
}

bool exportComparisonReport(const QString& path,
                            const QString& labelBefore, double pingBefore, double jitterBefore, double lossBefore, uint32_t samplesBefore,
                            const QString& labelAfter, double pingAfter, double jitterAfter, double lossAfter, uint32_t samplesAfter) {
    QImage img(kPageWidth, 1250, QImage::Format_RGB32);
    img.fill(bgPrimary());

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);

    drawHeader(p, QString::fromUtf8("Сравнительный замер «до / после» оптимизации"));

    int y = kHeaderH + 32;

    drawSectionTitle(p, y, QString::fromUtf8("Результаты замера"));
    y += 40;

    // two columns: before / after
    const int colW = (kPageWidth - 2 * kMargin - 24) / 2;
    const int ch = 150;

    auto drawColumn = [&](int x, const QString& label, double ping, double jitter, double loss, uint32_t samples) {
        QRect r(x, y, colW, ch);
        p.fillRect(r, bgSurface());
        p.setPen(QPen(QColor(255, 255, 255, 20), 1));
        p.drawRect(r);

        QFont lf("Segoe UI", 12, QFont::Bold);
        p.setFont(lf);
        p.setPen(textPrimary());
        p.drawText(r.adjusted(16, 10, -16, -90), Qt::AlignLeft | Qt::AlignVCenter, label);

        QFont vf("Segoe UI", 30, QFont::Bold);
        p.setFont(vf);
        p.setPen(accent());
        p.drawText(QRect(r.left() + 16, r.top() + 48, colW - 32, 50), Qt::AlignLeft | Qt::AlignVCenter,
                   QString("%1").arg(ping, 0, 'f', 1) + QString::fromUtf8(" мс"));

        QFont sf("Segoe UI", 11);
        p.setFont(sf);
        p.setPen(textSecondary());
        p.drawText(QRect(r.left() + 16, r.top() + 102, colW - 32, 24), Qt::AlignLeft | Qt::AlignVCenter,
                   QString::fromUtf8("Джиттер %1 мс · Потери %2% · Замеров: %3")
                       .arg(jitter, 0, 'f', 1)
                       .arg(loss, 0, 'f', 2)
                       .arg(samples));
    };

    drawColumn(kMargin, labelBefore, pingBefore, jitterBefore, lossBefore, samplesBefore);
    drawColumn(kMargin + colW + 24, labelAfter, pingAfter, jitterAfter, lossAfter, samplesAfter);
    y += ch + 30;

    // improvement bars
    drawSectionTitle(p, y, QString::fromUtf8("Изменение после оптимизации"));
    y += 42;

    struct Row { QString label; double before; double after; bool lowerBetter; QColor color; };
    std::vector<Row> rows = {
        {QString::fromUtf8("Пинг (мс)"), pingBefore, pingAfter, true, accent()},
        {QString::fromUtf8("Джиттер (мс)"), jitterBefore, jitterAfter, true, accentCyan()},
        {QString::fromUtf8("Потери пакетов (%)"), lossBefore, lossAfter, true, error()},
    };

    QFont lf("Segoe UI", 12);
    QFont vf("Segoe UI", 12, QFont::Bold);
    int rowY = y;
    for (const auto& row : rows) {
        p.setFont(lf);
        p.setPen(textPrimary());
        p.drawText(QRect(kMargin, rowY, 220, 26), Qt::AlignLeft | Qt::AlignVCenter, row.label);

        // comparison bar
        double maxV = std::max({row.before, row.after, 1.0});
        int barX = kMargin + 230;
        int barW = 460;
        int bh = 18;
        int by = rowY + 4;

        QRect beforeBar(barX, by, static_cast<int>(row.before / maxV * barW), bh);
        QRect afterBar(barX + 160, by, static_cast<int>(row.after / maxV * barW), bh);
        p.fillRect(beforeBar, QColor(148, 163, 184, 120));
        p.fillRect(afterBar, row.color);

        p.setFont(vf);
        p.setPen(row.color);
        p.drawText(QRect(kMargin + 700, rowY, 120, 26), Qt::AlignLeft | Qt::AlignVCenter,
                   QString("%1 → %2").arg(row.before, 0, 'f', 1).arg(row.after, 0, 'f', 1));
        rowY += 40;
    }

    y = rowY + 16;
    drawSectionTitle(p, y, QString::fromUtf8("Комментарий"));
    y += 36;
    QFont cf("Segoe UI", 12);
    p.setFont(cf);
    p.setPen(textSecondary());

    QString comment;
    double pingDiff = pingAfter - pingBefore;
    if (std::fabs(pingDiff) < 1.0)
        comment = QString::fromUtf8("Замеры проводились к 1.1.1.1 (Cloudflare). Пинг практически не изменился — "
                                    "разница в пределах погрешности. Снижение пинга в играх достигается за счёт "
                                    "маршрутизации через серверную сеть (раздел «Оптимизация сети»).");
    else if (pingDiff < 0)
        comment = QString::fromUtf8("Пинг снизился на %1 мс (%2%) — оптимизация работает. Проверьте, как изменились "
                                    "джиттер и потери пакетов: при стабильном маршруте они должны уменьшиться.")
                      .arg(-pingDiff, 0, 'f', 1)
                      .arg(-pingDiff / std::max(pingBefore, 0.1) * 100.0, 0, 'f', 0);
    else
        comment = QString::fromUtf8("Пинг вырос на %1 мс — возможно, сеть перегружена или маршрут изменился. "
                                    "Повторите замер позже или включите автовыбор маршрута.")
                      .arg(pingDiff, 0, 'f', 1);

    p.drawText(QRect(kMargin, y, kPageWidth - 2 * kMargin, 90),
               Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, comment);

    p.end();
    return img.save(path, "PNG");
}

bool exportHistoryReport(const QString& path,
                         const std::vector<SessionRecord>& records,
                         double avgPing, double avgJitter) {
    QImage img(kPageWidth, 1400, QImage::Format_RGB32);
    img.fill(bgPrimary());

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);

    drawHeader(p, QString::fromUtf8("История игровых сессий"));

    int y = kHeaderH + 32;

    const int cw = (kPageWidth - 2 * kMargin - 20) / 3;
    const int ch = 110;
    drawStatCard(p, kMargin, y, cw, ch, QString::fromUtf8("СЕССИЙ"), QString::number(records.size()), accent());
    drawStatCard(p, kMargin + cw + 10, y, cw, ch, QString::fromUtf8("СРЕДНИЙ ПИНГ"), QString("%1 мс").arg(avgPing, 0, 'f', 1), accentCyan());
    drawStatCard(p, kMargin + 2 * (cw + 10), y, cw, ch, QString::fromUtf8("СРЕДНИЙ ДЖИТТЕР"), QString("%1 мс").arg(avgJitter, 0, 'f', 1), textPrimary());
    y += ch + 30;

    drawSectionTitle(p, y, QString::fromUtf8("Последние сессии"));
    y += 38;

    QFont hf("Segoe UI", 11, QFont::Bold);
    QFont rf("Segoe UI", 11);

    p.setFont(hf);
    p.setPen(textSecondary());
    int colX[] = { kMargin, kMargin + 220, kMargin + 420, kMargin + 560, kMargin + 700, kMargin + 820 };
    p.drawText(QRect(colX[0], y, 200, 24), Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8("ИГРА"));
    p.drawText(QRect(colX[1], y, 180, 24), Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8("НАЧАЛО"));
    p.drawText(QRect(colX[2], y, 120, 24), Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8("ПИНГ"));
    p.drawText(QRect(colX[3], y, 120, 24), Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8("ДЖИТТЕР"));
    p.drawText(QRect(colX[4], y, 110, 24), Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8("ПОТЕРИ"));
    p.drawText(QRect(colX[5], y, 100, 24), Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8("УСКОР."));
    y += 30;

    int shown = 0;
    for (auto it = records.rbegin(); it != records.rend() && shown < 40; ++it, ++shown) {
        const auto& r = *it;
        if (shown % 2 == 0)
            p.fillRect(QRect(kMargin, y - 4, kPageWidth - 2 * kMargin, 28), QColor(255, 255, 255, 8));

        p.setFont(rf);
        p.setPen(textPrimary());
        p.drawText(QRect(colX[0], y, 200, 24), Qt::AlignLeft | Qt::AlignVCenter,
                   QString::fromStdString(r.game_name));
        p.setPen(textSecondary());
        p.drawText(QRect(colX[1], y, 180, 24), Qt::AlignLeft | Qt::AlignVCenter,
                   QString::fromStdString(r.start_time_str));
        p.setPen(accent());
        p.drawText(QRect(colX[2], y, 120, 24), Qt::AlignLeft | Qt::AlignVCenter,
                   QString("%1").arg(r.avg_ping_ms, 0, 'f', 1));
        p.setPen(accentCyan());
        p.drawText(QRect(colX[3], y, 120, 24), Qt::AlignLeft | Qt::AlignVCenter,
                   QString("%1").arg(r.avg_jitter_ms, 0, 'f', 1));
        p.setPen(r.avg_packet_loss > 2.0 ? error() : success());
        p.drawText(QRect(colX[4], y, 110, 24), Qt::AlignLeft | Qt::AlignVCenter,
                   QString("%1%").arg(r.avg_packet_loss, 0, 'f', 1));
        p.setPen(r.boost_was_active ? success() : textSecondary());
        p.drawText(QRect(colX[5], y, 100, 24), Qt::AlignLeft | Qt::AlignVCenter,
                   r.boost_was_active ? QString::fromUtf8("ДА") : QString::fromUtf8("—"));
        y += 28;
    }

    p.end();
    return img.save(path, "PNG");
}

} // namespace report
} // namespace gno#include <QWidget>