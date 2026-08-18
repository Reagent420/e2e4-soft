#pragma once

#include <QColor>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QString>
#include <QVector>
#include <QWidget>

struct GameInfo {
    QString name;
    QString category;
    bool installed;
    bool running;
    QColor iconColor;
};

class GameListWidget : public QWidget {
    Q_OBJECT
public:
    explicit GameListWidget(QWidget* parent = nullptr);

signals:
    void gameSelected(const QString& gameName, const QString& category);

private slots:
    void onSearchChanged(const QString& text);
    void onGameCardClicked();

private:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void buildGameCards();
    void updateStatus();

    QLineEdit* search_box_;
    QGridLayout* grid_layout_;
    QVector<GameInfo> games_;
    QVector<QWidget*> game_cards_;
    QWidget* grid_container_;
    QLabel* status_label_;
};
