#pragma once

#include <QWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QLabel>
#include <QVBoxLayout>

namespace gno {

class GameProfiles;
class GameDetector;

class GameProfilesWidget : public QWidget {
    Q_OBJECT

public:
    explicit GameProfilesWidget(QWidget* parent = nullptr);

private slots:
    void onGameSelected(int index);
    void onSaveProfile();
    void refreshProfileList();

private:
    void setupUI();

    GameProfiles* m_profiles;
    GameDetector* m_detector;

    QComboBox* m_gameCombo;
    QCheckBox* m_multipathCb;
    QCheckBox* m_fpsBoostCb;
    QCheckBox* m_networkOptCb;
    QCheckBox* m_autoApplyCb;
    QSpinBox* m_maxRoutesSpin;
    QWidget* m_profileList;
    QLabel* m_statusLabel;
};

} // namespace gno