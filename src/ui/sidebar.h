#pragma once

#include <QWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QButtonGroup>
#include <QLabel>

namespace gno {

class Sidebar : public QWidget {
    Q_OBJECT

public:
    explicit Sidebar(QWidget* parent = nullptr);
    ~Sidebar() override = default;

    void setNavigationIndex(int index);

signals:
    void navigationChanged(int index);

private:
    QPushButton* createNavButton(const QString& icon, const QString& text);

    QButtonGroup* m_buttonGroup;
    QLabel* m_logoLabel;
    QLabel* m_versionLabel;
    static constexpr int BUTTON_COUNT = 5;
};

} // namespace gno
