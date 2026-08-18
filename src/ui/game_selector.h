#pragma once

#include <QWidget>
#include <QListWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>
#include "../core/game_detector.h"

namespace gno {
namespace ui {

class GameSelector : public QWidget {
    Q_OBJECT

public:
    explicit GameSelector(QWidget* parent = nullptr);
    ~GameSelector();

    void loadGames(const std::vector<GameInfo>& games);
    void setSelectedGame(const GameInfo& game);
    GameInfo getSelectedGame() const;

signals:
    void gameSelected(const GameInfo& game);
    void regionChanged(const QString& region);

private slots:
    void onGameItemClicked(QListWidgetItem* item);
    void onRegionChanged(int index);
    void onSearchTextChanged(const QString& text);
    void onRefreshClicked();

private:
    void setupUI();
    void updateRegionList(const std::string& game_name);
    void filterGames(const QString& filter);

    QLineEdit* search_box_;
    QListWidget* game_list_;
    QComboBox* region_combo_;
    QLabel* game_icon_label_;
    QLabel* game_name_label_;
    QLabel* game_info_label_;
    QPushButton* refresh_button_;
    
    std::vector<GameInfo> all_games_;
    std::vector<GameRegion> current_regions_;
    GameInfo selected_game_;
};

} // namespace ui
} // namespace gno
