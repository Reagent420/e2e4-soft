#pragma once

#include <QWidget>
#include <QTimer>
#include <QPoint>
#include <QLabel>

namespace gno {

class MonitoringService;

// In-game overlay: small always-on-top panel showing live ping / jitter /
// packet loss, the running game and session time. Draggable, has a context
// menu (position, opacity) and a global F9 hotkey (Windows).
class OverlayWidget : public QWidget {
    Q_OBJECT
public:
    enum class Corner { TopLeft, TopRight, BottomLeft, BottomRight };

    explicit OverlayWidget(QWidget* parent = nullptr);
    ~OverlayWidget() override;

    void setCorner(Corner corner);
    void setOpacityPercent(int percent);
    void toggleOverlay();
    void showOverlay();
    void hideOverlay();

signals:
    void overlayToggled(bool on);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void refresh();

private:
    void applyPosition();
    void registerHotkey();
    void unregisterHotkey();

    MonitoringService* service_ = nullptr;
    Corner corner_ = Corner::TopRight;
    int opacity_percent_ = 85;

    QLabel* ping_lbl_ = nullptr;
    QLabel* jitter_lbl_ = nullptr;
    QLabel* loss_lbl_ = nullptr;
    QLabel* game_lbl_ = nullptr;
    QLabel* timer_lbl_ = nullptr;
    QLabel* boost_lbl_ = nullptr;
    QTimer* timer_ = nullptr;

    bool dragging_ = false;
    QPoint drag_offset_;
    QPoint custom_pos_;
    bool custom_position_ = false;
    bool hotkey_registered_ = false;
};

} // namespace gno