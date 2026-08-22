#pragma once

#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QTimer>
#include <QTextEdit>
#include <QPushButton>

#include "core/server_map_model.h"

class QPaintEvent;
class QMouseEvent;

namespace gno {

// Live server map: real nodes projected from lat/lon, on-demand ICMP probing,
// region filters, auto-refresh and a details card. Cyber-styled via theme.
class GeoMapWidget : public QWidget {
    Q_OBJECT

public:
    explicit GeoMapWidget(QWidget* parent = nullptr);

signals:
    void probeFinished();
    void probeProgress(const QString& text);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

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

    // map canvas geometry helpers (widget == canvas; controls live beside)
    QPointF nodePos(const MapServer& s) const;
    int pickNode(const QPoint& pos) const;

    std::vector<MapServer> servers_;   // full list
    std::vector<int> visible_;         // indices into servers_
    int selected_ = -1;                // index into servers_
    int best_ = -1;

    QComboBox* m_region_;
    QCheckBox* m_labels_;
    QCheckBox* m_grid_;
    QComboBox* m_interval_;
    QPushButton* m_check_all_btn_;
    QPushButton* m_check_sel_btn_;
    QLabel* m_progress_;
    QTextEdit* m_details_;
    QTimer* m_timer_;
};

} // namespace gno
