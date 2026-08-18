#include "game_selector.h"
#include <algorithm>

namespace gno {
namespace ui {

GameSelector::GameSelector(QWidget* parent)
    : QWidget(parent) {
    setupUI();
}

GameSelector::~GameSelector() = default;

void GameSelector::setupUI() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    
    QHBoxLayout* search_layout = new QHBoxLayout();
    search_box_ = new QLineEdit();
    search_box_->setPlaceholderText("Search games...");
    connect(search_box_, &QLineEdit::textChanged,
            this, &GameSelector::onSearchTextChanged);
    search_layout->addWidget(search_box_);
    
    refresh_button_ = new QPushButton("↻");
    refresh_button_->setFixedWidth(32);
    refresh_button_->setStyleSheet("padding: 4px;");
    connect(refresh_button_, &QPushButton::clicked,
            this, &GameSelector::onRefreshClicked);
    search_layout->addWidget(refresh_button_);
    
    layout->addLayout(search_layout);
    
    game_list_ = new QListWidget();
    game_list_->setStyleSheet(
        "QListWidget { font-size: 12px; }"
        "QListWidget::item { padding: 8px; border-bottom: 1px solid #333; }"
    );
    connect(game_list_, &QListWidget::itemClicked,
            this, &GameSelector::onGameItemClicked);
    layout->addWidget(game_list_);
    
    QHBoxLayout* info_layout = new QHBoxLayout();
    game_icon_label_ = new QLabel();
    game_icon_label_->setFixedSize(48, 48);
    game_icon_label_->setStyleSheet("background-color: #1a1a1a; border-radius: 4px;");
    info_layout->addWidget(game_icon_label_);
    
    QVBoxLayout* text_layout = new QVBoxLayout();
    game_name_label_ = new QLabel("Select a game");
    game_name_label_->setStyleSheet("font-size: 14px; font-weight: bold;");
    text_layout->addWidget(game_name_label_);
    
    game_info_label_ = new QLabel("");
    game_info_label_->setStyleSheet("font-size: 11px; color: #888;");
    text_layout->addWidget(game_info_label_);
    
    info_layout->addLayout(text_layout);
    info_layout->addStretch();
    
    layout->addLayout(info_layout);
    
    QHBoxLayout* region_layout = new QHBoxLayout();
    QLabel* region_label = new QLabel("Region:");
    region_label_->setStyleSheet("font-size: 11px;");
    region_layout->addWidget(region_label);
    
    region_combo_ = new QComboBox();
    region_combo_->addItem("Auto Detect");
    connect(region_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &GameSelector::onRegionChanged);
    region_layout->addWidget(region_combo_);
    
    layout->addLayout(region_layout);
}

void GameSelector::loadGames(const std::vector<GameInfo>& games) {
    all_games_ = games;
    game_list_->clear();
    
    std::sort(all_games_.begin(), all_games_.end(),
              [](const GameInfo& a, const GameInfo& b) { return a.name < b.name; });
    
    for (const auto& game : all_games_) {
        QString display = QString::fromStdString(game.name);
        if (game.is_running) {
            display += " ●";
        } else if (game.is_installed) {
            display += " ○";
        }
        
        QListWidgetItem* item = new QListWidgetItem(display);
        item->setData(Qt::UserRole, QVariant::fromValue(game.name));
        
        if (game.is_running) {
            item->setForeground(QBrush(QColor("#4ecdc4")));
        } else if (game.is_installed) {
            item->setForeground(QBrush(QColor("#ffd93d")));
        }
        
        game_list_->addItem(item);
    }
}

void GameSelector::setSelectedGame(const GameInfo& game) {
    selected_game_ = game;
    game_name_label_->setText(QString::fromStdString(game.name));
    
    QString info;
    if (game.is_running) info += "Running";
    else if (game.is_installed) info += "Installed";
    else info += "Not Installed";
    
    info += " | " + QString::fromStdString(game.category);
    game_info_label_->setText(info);
    
    updateRegionList(game.name);
}

GameInfo GameSelector::getSelectedGame() const {
    return selected_game_;
}

void GameSelector::onGameItemClicked(QListWidgetItem* item) {
    QString game_name = item->data(Qt::UserRole).toString();
    
    for (const auto& game : all_games_) {
        if (QString::fromStdString(game.name) == game_name) {
            setSelectedGame(game);
            emit gameSelected(game);
            break;
        }
    }
}

void GameSelector::onRegionChanged(int index) {
    if (index > 0 && index < current_regions_.size()) {
        emit regionChanged(QString::fromStdString(current_regions_[index].name));
    } else {
        emit regionChanged("auto");
    }
}

void GameSelector::onSearchTextChanged(const QString& text) {
    filterGames(text);
}

void GameSelector::onRefreshClicked() {
    GameDetector detector;
    detector.scanInstalledGames();
    detector.detectRunningGames();
    loadGames(detector.getSupportedGames());
}

void GameSelector::updateRegionList(const std::string& game_name) {
    region_combo_->clear();
    region_combo_->addItem("Auto Detect");
    
    GameDetector detector;
    current_regions_ = detector.getRegionsForGame(game_name);
    
    for (const auto& region : current_regions_) {
        region_combo_->addItem(QString::fromStdString(region.display_name));
    }
}

void GameSelector::filterGames(const QString& filter) {
    for (int i = 0; i < game_list_->count(); i++) {
        QListWidgetItem* item = game_list_->item(i);
        bool match = filter.isEmpty() || 
                    item->text().contains(filter, Qt::CaseInsensitive);
        item->setHidden(!match);
    }
}

} // namespace ui
} // namespace gno
