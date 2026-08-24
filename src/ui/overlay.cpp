#include "overlay.h"
#include "../core/connection_grader.h"
#include "monitoring_service.h"
#include "theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QMenu>
#include <QMouseEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <QContextMenuEvent>
#include <QScreen>
#include <QGuiApplication>
#include <QFont>
#include <QDateTime>
#include <QVBoxLayout>

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#undef ERROR
#endif

namespace gno {

namespace {
constexpr int kOverlayWidth = 236;
constexpr int kOverlayHeight = 168;
constexpr int kMargin = 16;
}

OverlayWidget::OverlayWidget(QWidget* parent)
    : QWidget(parent) {
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFixedSize(kOverlayWidth, kOverlayHeight);
    setCursor(Qt::OpenHandCursor);

    service_ = &MonitoringService::instance();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 12, 14, 10);
    layout->setSpacing(2);

    game_lbl_ = new QLabel(QString::fromUtf8("Мониторинг сети"), this);
    game_lbl_->setStyleSheet(QString("color:%1; font-size:11px; font-weight:700; background:transparent;")
                                 .arg(theme::Colors::ACCENT_CYAN));
    layout->addWidget(game_lbl_);

    ping_lbl_ = new QLabel(QString::fromUtf8("Пинг: —"), this);
    ping_lbl_->setStyleSheet(QString("color:%1; font-size:13px; font-weight:600; background:transparent;")
                                 .arg(theme::Colors::TEXT_PRIMARY));
    layout->addWidget(ping_lbl_);

    jitter_lbl_ = new QLabel(QString::fromUtf8("Джиттер: —"), this);
    jitter_lbl_->setStyleSheet(QString("color:%1; font-size:12px; background:transparent;")
                                   .arg(theme::Colors::TEXT_SECONDARY));
    layout->addWidget(jitter_lbl_);

    loss_lbl_ = new QLabel(QString::fromUtf8("Потери: —"), this);
    loss_lbl_->setStyleSheet(QString("color:%1; font-size:12px; background:transparent;")
                                 .arg(theme::Colors::TEXT_SECONDARY));
    layout->addWidget(loss_lbl_);
    score_lbl_ = new QLabel(QStringLiteral("-"), this);
    score_lbl_->setStyleSheet(QString("color:%1; font-size:12px; font-weight:700; background:transparent;")
                                 .arg(theme::Colors::ACCENT_NEON));
    layout->addWidget(score_lbl_);

    timer_lbl_ = new QLabel(QString::fromUtf8("Сессия: —"), this);
    timer_lbl_->setStyleSheet(QString("color:%1; font-size:11px; background:transparent;")
                                  .arg(theme::Colors::TEXT_TERTIARY));
    layout->addWidget(timer_lbl_);

    boost_lbl_ = new QLabel(this);
    boost_lbl_->setStyleSheet(QString("color:%1; font-size:11px; font-weight:600; background:transparent;")
                                  .arg(theme::Colors::SUCCESS));
    layout->addWidget(boost_lbl_);
    layout->addStretch();

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &OverlayWidget::refresh);
    timer_->start(500);

    refresh();
    setOpacityPercent(opacity_percent_);
}

OverlayWidget::~OverlayWidget() {
    unregisterHotkey();
}

void OverlayWidget::setCorner(Corner corner) {
    corner_ = corner;
    custom_position_ = false;
    applyPosition();
}

void OverlayWidget::setOpacityPercent(int percent) {
    opacity_percent_ = qBound(40, percent, 100);
    setWindowOpacity(opacity_percent_ / 100.0);
}

void OverlayWidget::toggleOverlay() {
    if (isVisible())
        hideOverlay();
    else
        showOverlay();
    emit overlayToggled(isVisible());
}

void OverlayWidget::showOverlay() {
    applyPosition();
    show();
    raise();
    registerHotkey();
    emit overlayToggled(true);
}

void OverlayWidget::hideOverlay() {
    hide();
    unregisterHotkey();
    emit overlayToggled(false);
}

void OverlayWidget::refresh() {
    double ping = service_->currentPing();
    bool ok = service_->hasPing();
    double jitter = service_->currentJitter();
    double loss = service_->currentLossPercent();

    QString pingText;
    if (ok) {
        QColor c = ping < 35 ? QColor(theme::Colors::SUCCESS)
                             : ping < 50 ? QColor(theme::Colors::WARNING)
                                         : QColor(theme::Colors::ERROR);
        pingText = QString::fromUtf8("Пинг: <span style='color:%1; font-size:15px; font-weight:700;'>%2</span> мс")
                       .arg(c.name())
                       .arg(static_cast<int>(ping));
    } else {
        pingText = QString::fromUtf8("Пинг: <span style='color:%1;'>тайм-аут</span>")
                       .arg(theme::Colors::ERROR);
    }
    ping_lbl_->setText(pingText);

    QColor jc = jitter < 2 ? QColor(theme::Colors::SUCCESS)
                           : jitter < 5 ? QColor(theme::Colors::WARNING)
                                        : QColor(theme::Colors::ERROR);
    jitter_lbl_->setText(QString::fromUtf8("Джиттер: <span style='color:%1;'>%2</span> мс")
                             .arg(jc.name())
                             .arg(jitter, 0, 'f', 1));

    QColor lc = loss < 0.1 ? QColor(theme::Colors::SUCCESS)
                           : loss < 1.0 ? QColor(theme::Colors::WARNING)
                                        : QColor(theme::Colors::ERROR);
    loss_lbl_->setText(QString::fromUtf8("Потери: <span style='color:%1;'>%2</span>%")
                           .arg(lc.name())
                           .arg(loss, 0, 'f', 1));

    const int tray_score = static_cast<int>(ConnectionGrader::evaluate(
        service_->currentPing(), service_->currentJitter(), service_->currentLossPercent()).score);
    score_lbl_->setText(QString::fromUtf8("\xD0\x9E\xD1\x86\xD0\xB5\xD0\xBD\xD0\xBA\xD0\xB0\x3A\x20") +
                         QString::number(tray_score) + QStringLiteral(" / 100"));

    QString game = service_->currentGame();
    if (!game.isEmpty())
        game_lbl_->setText(QString::fromUtf8("Игра: %1").arg(game));
    else
        game_lbl_->setText(QString::fromUtf8("Мониторинг сети"));

    if (service_->isBoostActive())
        boost_lbl_->setText(QString::fromUtf8("⚡ Оптимизация: ВКЛ"));
    else
        boost_lbl_->setText(QString::fromUtf8("Оптимизация: ВЫКЛ"));

    // session timer
    if (!game.isEmpty()) {
        // session duration is tracked by the service via its own clock; here we
        // approximate using the elapsed time from the start notification by
        // re-reading the record start time through history.
        auto records = service_->history()->getLast(1);
        QString dur;
        if (!records.empty()) {
            QDateTime start = QDateTime::fromString(QString::fromStdString(records.back().start_time_str),
                                                    "yyyy-MM-dd HH:mm:ss");
            if (start.isValid()) {
                qint64 secs = start.secsTo(QDateTime::currentDateTime());
                if (secs < 0) secs = 0;
                dur = QString::fromUtf8("Сессия: %1:%2")
                          .arg(secs / 60, 2, 10, QLatin1Char('0'))
                          .arg(secs % 60, 2, 10, QLatin1Char('0'));
            }
        }
        if (dur.isEmpty())
            dur = QString::fromUtf8("Сессия: 00:00");
        timer_lbl_->setText(dur);
    } else {
        timer_lbl_->setText(QString::fromUtf8("Сессия: —"));
    }

    update();
}

void OverlayWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QRect r = rect().adjusted(1, 1, -1, -1);
    QPainterPath path;
    path.addRoundedRect(r, 14, 14);

    QLinearGradient grad(r.topLeft(), r.bottomLeft());
    grad.setColorAt(0.0, QColor(26, 26, 46, 230));
    grad.setColorAt(1.0, QColor(13, 13, 15, 230));
    p.fillPath(path, grad);

    p.setPen(QPen(QColor(59, 130, 246, 90), 1));
    p.drawPath(path);
}

void OverlayWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        dragging_ = true;
        drag_offset_ = event->globalPosition().toPoint() - pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void OverlayWidget::mouseMoveEvent(QMouseEvent* event) {
    if (dragging_) {
        move(event->globalPosition().toPoint() - drag_offset_);
        custom_position_ = true;
        custom_pos_ = pos();
    }
}

void OverlayWidget::mouseReleaseEvent(QMouseEvent* event) {
    Q_UNUSED(event);
    dragging_ = false;
    setCursor(Qt::OpenHandCursor);
}

void OverlayWidget::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);

    auto addCornerAction = [&](const QString& text, Corner c) {
        QAction* a = menu.addAction(text);
        a->setCheckable(true);
        a->setChecked(!custom_position_ && corner_ == c);
        connect(a, &QAction::triggered, this, [this, c]() { setCorner(c); });
        return a;
    };
    addCornerAction(QString::fromUtf8("Верхний левый угол"), Corner::TopLeft);
    addCornerAction(QString::fromUtf8("Верхний правый угол"), Corner::TopRight);
    addCornerAction(QString::fromUtf8("Нижний левый угол"), Corner::BottomLeft);
    addCornerAction(QString::fromUtf8("Нижний правый угол"), Corner::BottomRight);

    menu.addSeparator();
    for (int op : {60, 75, 85, 95, 100}) {
        QAction* a = menu.addAction(QString::fromUtf8("Прозрачность: %1%").arg(op));
        a->setCheckable(true);
        a->setChecked(opacity_percent_ == op);
        connect(a, &QAction::triggered, this, [this, op]() { setOpacityPercent(op); });
    }

    menu.addSeparator();
    QAction* hide = menu.addAction(QString::fromUtf8("Скрыть оверлей (F9)"));
    connect(hide, &QAction::triggered, this, &OverlayWidget::hideOverlay);

    menu.exec(event->globalPos());
}

bool OverlayWidget::nativeEvent(const QByteArray& eventType, void* message, qintptr* result) {
#ifdef PLATFORM_WINDOWS
    Q_UNUSED(eventType);
    MSG* msg = static_cast<MSG*>(message);
    if (msg->message == WM_HOTKEY && msg->wParam == 1) {
        toggleOverlay();
        if (result) *result = 0;
        return true;
    }
#endif
    return QWidget::nativeEvent(eventType, message, result);
}

void OverlayWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    registerHotkey();
}

void OverlayWidget::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    unregisterHotkey();
}

void OverlayWidget::applyPosition() {
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) return;
    QRect screenRect = screen->availableGeometry();

    QPoint p;
    switch (corner_) {
        case Corner::TopLeft:     p = QPoint(kMargin, kMargin); break;
        case Corner::TopRight:    p = QPoint(screenRect.right() - width() - kMargin, kMargin); break;
        case Corner::BottomLeft:  p = QPoint(kMargin, screenRect.bottom() - height() - kMargin); break;
        case Corner::BottomRight: p = QPoint(screenRect.right() - width() - kMargin, screenRect.bottom() - height() - kMargin); break;
    }

    if (custom_position_)
        p = custom_pos_;

    move(p);
}

void OverlayWidget::registerHotkey() {
#ifdef PLATFORM_WINDOWS
    if (hotkey_registered_) return;
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (RegisterHotKey(hwnd, 1, MOD_NOREPEAT, VK_F9))
        hotkey_registered_ = true;
#endif
}

void OverlayWidget::unregisterHotkey() {
#ifdef PLATFORM_WINDOWS
    if (!hotkey_registered_) return;
    HWND hwnd = reinterpret_cast<HWND>(winId());
    UnregisterHotKey(hwnd, 1);
    hotkey_registered_ = false;
#endif
}

} // namespace gno