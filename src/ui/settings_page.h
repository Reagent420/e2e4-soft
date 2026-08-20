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

private slots:
    void onResetDefaults();

private:
    QCheckBox* minimize_tray_;
    QCheckBox* show_notifications_;
    QComboBox* language_;
    QComboBox* theme_;
    QCheckBox* verbose_log_;
    QCheckBox* dev_mode_;
};
