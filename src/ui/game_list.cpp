#include "game_list.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QScrollArea>
#include <QStyle>
#include <QVBoxLayout>

GameListWidget::GameListWidget(QWidget* parent)
    : QWidget(parent) {
    auto* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(20, 16, 20, 16);
    main_layout->setSpacing(12);

    auto* header = new QHBoxLayout();
    auto* title = new QLabel("Game Library");
    title->setObjectName("pageHeader");
    auto* subtitle = new QLabel("Auto-detected games");
    subtitle->setObjectName("pageSubtitle");
    header->addWidget(title);
    header->addStretch();
    header->addWidget(subtitle);
    main_layout->addLayout(header);

    search_box_ = new QLineEdit();
    search_box_->setObjectName("searchBox");
    search_box_->setPlaceholderText("Search games...");
    main_layout->addWidget(search_box_);
    connect(search_box_, &QLineEdit::textChanged, this, &GameListWidget::onSearchChanged);

    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setObjectName("gameScrollArea");

    grid_container_ = new QWidget();
    grid_container_->setObjectName("gameGridContainer");
    grid_layout_ = new QGridLayout(grid_container_);
    grid_layout_->setSpacing(12);
    grid_layout_->setContentsMargins(0, 0, 0, 0);
    grid_layout_->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    scroll->setWidget(grid_container_);
    main_layout->addWidget(scroll, 1);

    status_label_ = new QLabel();
    status_label_->setObjectName("gameStatusBar");
    main_layout->addWidget(status_label_);

    buildGameCards();
    updateStatus();
}

void GameListWidget::buildGameCards() {
    games_ = {
        {"Counter-Strike 2",  "FPS",       true,  false, QColor(0, 200, 255)},
        {"Dota 2",            "MOBA",      true,  false, QColor(160, 80, 220)},
        {"VALORANT",          "FPS",       true,  false, QColor(0, 200, 255)},
        {"Fortnite",          "BR",        false, false, QColor(0, 200, 100)},
        {"Apex Legends",      "BR",        false, false, QColor(0, 200, 100)},
        {"PUBG",              "BR",        false, false, QColor(0, 200, 100)},
        {"Overwatch 2",       "FPS",       false, false, QColor(0, 200, 255)},
        {"League of Legends", "MOBA",      false, false, QColor(160, 80, 220)},
        {"Rocket League",     "Racing",    false, false, QColor(220, 160, 0)},
        {"Minecraft",         "Sandbox",   false, false, QColor(220, 160, 0)},
        {"Rust",              "Survival",  false, false, QColor(220, 160, 0)},
        {"Rainbow Six Siege", "FPS",       false, false, QColor(0, 200, 255)},
        {"Escape from Tarkov","FPS",       false, false, QColor(0, 200, 255)},
        {"Call of Duty: Warzone","FPS",    false, false, QColor(0, 200, 255)},
        {"Destiny 2",         "FPS",       false, false, QColor(0, 200, 255)},
        {"World of Warcraft", "MMO",       false, false, QColor(160, 80, 220)},
        {"Final Fantasy XIV", "MMO",       false, false, QColor(160, 80, 220)},
        {"Path of Exile",     "ARPG",      false, false, QColor(220, 160, 0)},
        {"Dead by Daylight",  "Horror",    false, false, QColor(220, 60, 60)},
        {"Fall Guys",         "Party",     false, false, QColor(0, 200, 100)},
    };

    const int cols = 4;
    for (int i = 0; i < games_.size(); ++i) {
        const auto& game = games_[i];

        auto* card = new QWidget();
        card->setObjectName("gameCard");
        card->setFixedSize(150, 160);
        card->setCursor(Qt::PointingHandCursor);
        card->installEventFilter(this);
        card->setProperty("gameIndex", i);

        auto* card_layout = new QVBoxLayout(card);
        card_layout->setContentsMargins(10, 10, 10, 10);
        card_layout->setSpacing(6);

        auto* icon = new QWidget();
        icon->setObjectName("gameIcon");
        icon->setFixedSize(48, 48);
        QPalette pal = icon->palette();
        pal.setColor(QPalette::Window, game.iconColor);
        icon->setPalette(pal);
        icon->setAutoFillBackground(true);
        icon->setStyleSheet(QString("border-radius: 8px; background-color: %1;").arg(game.iconColor.name()));
        card_layout->addWidget(icon, 0, Qt::AlignHCenter);

        auto* name_label = new QLabel(game.name);
        name_label->setObjectName("gameTitle");
        name_label->setAlignment(Qt::AlignCenter);
        name_label->setWordWrap(true);
        name_label->setMaximumHeight(32);
        card_layout->addWidget(name_label);

        auto* category_label = new QLabel(game.category);
        category_label->setObjectName("gameCategory");
        category_label->setAlignment(Qt::AlignCenter);
        card_layout->addWidget(category_label);

        QString status_text;
        QString status_color;
        if (game.running) {
            status_text = "● Running";
            status_color = "#00e676";
        } else if (game.installed) {
            status_text = "● Installed";
            status_color = "#00bcd4";
        } else {
            status_text = "● Detected";
            status_color = "#888888";
        }

        auto* status = new QLabel(status_text);
        status->setObjectName("gameStatus");
        status->setAlignment(Qt::AlignCenter);
        status->setStyleSheet(QString("color: %1; font-size: 11px;").arg(status_color));
        card_layout->addWidget(status);

        card_layout->addStretch();

        grid_layout_->addWidget(card, i / cols, i % cols);
        game_cards_.append(card);
    }
}

void GameListWidget::onSearchChanged(const QString& text) {
    int visible = 0;
    for (int i = 0; i < game_cards_.size(); ++i) {
        bool match = text.isEmpty() ||
                     games_[i].name.contains(text, Qt::CaseInsensitive) ||
                     games_[i].category.contains(text, Qt::CaseInsensitive);
        game_cards_[i]->setVisible(match);
        if (match) ++visible;
    }
    updateStatus();
}

void GameListWidget::onGameCardClicked() {
    auto* card = qobject_cast<QWidget*>(sender());
    if (!card) return;
    int idx = card->property("gameIndex").toInt();
    if (idx < 0 || idx >= games_.size()) return;

    for (auto* c : game_cards_)
        c->setObjectName("gameCard");

    card->setObjectName("gameCardSelected");
    card->style()->unpolish(card);
    card->style()->polish(card);

    emit gameSelected(games_[idx].name, games_[idx].category);
}

bool GameListWidget::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::MouseButtonPress) {
        auto* card = qobject_cast<QWidget*>(obj);
        if (card && game_cards_.contains(card)) {
            onGameCardClicked();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void GameListWidget::updateStatus() {
    int total = 0;
    int installed = 0;
    int running = 0;
    for (int i = 0; i < games_.size(); ++i) {
        if (game_cards_[i]->isVisible()) {
            ++total;
            if (games_[i].installed) ++installed;
            if (games_[i].running) ++running;
        }
    }
    status_label_->setText(
        QString("Showing %1 games  |  %2 installed  |  %3 running")
            .arg(total).arg(installed).arg(running));
}
