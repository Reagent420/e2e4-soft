#pragma once

#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QTimer>
#include <QPushButton>

#include "core/server_map_model.h"

#include <functional>
#include <vector>

class QTextEdit;

namespace gno {

// Dedicated drawing surface: stylized wireframe world + live server nodes.
// Reports picks through a callback. Never overlaps the settings panel.
class MapCanvas : public QWidget {
public:
    MapCanvas(QWidget* parent = nullptr);

    void bind(std::vector<MapServer>* servers,
              std::vector<int>* visible,
              int* selected,
              int* best,
              const bool* show_labels,
              const bool* show_grid);

    void setPulsePhase(bool on); // toggles selected-node glow

    std::function<void()> onClicked;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QPointF project(double lon, double lat) const;
    int pickNode(const QPoint& pos) const;
    void drawWorld(QPainter& p) const;

    std::vector<MapServer>* servers_ = nullptr;
    std::vector<int>* visible_ = nullptr;
    int* selected_ = nullptr;
    int* best_ = nullptr;
    const bool* show_labels_ = nullptr;
    const bool* show_grid_ = nullptr;
    bool pulse_on_ = false;
};

// Page container: canvas left, settings panel right.
class GeoMapWidget : public QWidget {
    Q_OBJECT

public:
    explicit GeoMapWidget(QWidget* parent = nullptr);

signals:
    void probeProgress(const QString& text);

protected:
    void showEvent(QShowEvent* event) override;

private slots:
    void onCheckAllClicked();
    void onCheckSelectedClicked();

private:
    void setupUI();
    void loadSettings();
    void saveSettings();
    void rebuildVisibleServers();
    void updateDetailsCard();
    void startProbeThread(bool all);

    std::vector<MapServer> servers_;
    std::vector<int> visible_;
    int selected_ = -1;
    int best_ = -1;
    bool first_probe_done_ = false;
    bool m_labels_shown_ = true;
    bool m_grid_shown_ = true;
    bool pulse_phase_ = false;

    MapCanvas* canvas_ = nullptr;
    QComboBox* m_region_ = nullptr;
    QCheckBox* m_labels_ = nullptr;
    QCheckBox* m_grid_ = nullptr;
    QComboBox* m_interval_ = nullptr;
    QPushButton* m_check_all_btn_ = nullptr;
    QPushButton* m_check_sel_btn_ = nullptr;
    QLabel* m_progress_ = nullptr;
    QTextEdit* m_details_ = nullptr;
    QTimer* m_timer_ = nullptr;
    QTimer* m_pulse_timer_ = nullptr;
};

} // namespace gno
