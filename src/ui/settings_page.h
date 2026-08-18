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

private slots:
    void onResetDefaults();

private:
    QCheckBox* start_windows_;
    QCheckBox* minimize_tray_;
    QCheckBox* show_notifications_;
    QComboBox* language_;
    QComboBox* protocol_;
    QComboBox* region_;
    QComboBox* max_routes_;
    QComboBox* ping_interval_;
    QCheckBox* verbose_log_;
    QCheckBox* auto_update_;
    QCheckBox* dev_mode_;
};
