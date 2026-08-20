#pragma once

#include <QWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>

class SettingsPageWidget : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPageWidget(QWidget* parent = nullptr);

signals:
    void themeChanged(bool dark);
    void notificationsChanged(bool enabled);
    void soundChanged(bool enabled);
    void overlayChanged(bool enabled, int corner, int opacity);
    void startWithWindowsChanged(bool enabled);

private slots:
    void onResetDefaults();

private:
    void setupUI();
    void loadSettings();
    void saveSettings();
    void applyStartWithWindows(bool enabled);

    QCheckBox* start_windows_;
    QCheckBox* minimize_tray_;
    QCheckBox* show_notifications_;
    QCheckBox* sound_notifications_;
    QCheckBox* overlay_enabled_;
    QComboBox* language_;
    QComboBox* theme_;
    QComboBox* protocol_;
    QComboBox* region_;
    QComboBox* max_routes_;
    QComboBox* ping_interval_;
    QComboBox* overlay_corner_;
    QComboBox* overlay_opacity_;
    QCheckBox* verbose_log_;
    QCheckBox* auto_update_;
    QCheckBox* dev_mode_;

    // pro presets section
    QComboBox* pro_game_combo_;
    QLabel* pro_desc_label_;
    QLabel* pro_status_label_;
};