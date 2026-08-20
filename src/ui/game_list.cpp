#include "game_list.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

static QString translateCategory(const QString& cat) {
    const QString c = cat.toLower();
    if (c == "fps") return "Шутер";
    if (c == "moba") return "MOBA";
    if (c == "battle royale" || c == "br") return "Королевская битва";
    if (c == "mmorpg" || c == "mmo") return "MMORPG";
    if (c == "rpg") return "RPG";
    if (c == "sports") return "Спорт";
    if (c == "racing") return "Гонки";
    if (c == "survival") return "Выживание";
    if (c == "sandbox") return "Песочница";
    if (c == "horror") return "Хоррор";
    if (c == "party") return "Вечеринка";
    if (c == "fighting") return "Файтинг";
    if (c == "arpg") return "ARPG";
    if (c == "simulation") return "Симулятор";
    if (c == "strategy") return "Стратегия";
    return cat;
}

QColor GameListWidget::categoryColor(const QString& category) {
    const QString c = category.toLower();
    if (c == "fps") return QColor(0, 200, 255);
    if (c == "moba" || c == "mmorpg" || c == "mmo" || c == "rpg" || c == "arpg") return QColor(160, 80, 220);
    if (c == "battle royale" || c == "br" || c == "party" || c == "sandbox") return QColor(0, 200, 100);
    if (c == "racing" || c == "sports" || c == "survival") return QColor(220, 160, 0);
    if (c == "horror" || c == "fighting" || c == "strategy" || c == "simulation") return QColor(220, 60, 60);
    return QColor(100, 116, 139);
}

GameListWidget::GameListWidget(QWidget* parent)
    : QWidget(parent) {
    auto* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(20, 16, 20, 16);
    main_layout->setSpacing(12);

    auto* header = new QHBoxLayout();
    auto* title = new QLabel("Игры");
    title->setObjectName("pageHeader");
    auto* subtitle = new QLabel("Обнаруженные и установленные игры (Steam / Epic / GOG)");
    subtitle->setObjectName("pageSubtitle");
    header->addWidget(title);
    header->addStretch();
    header->addWidget(subtitle);
    main_layout->addLayout(header);

    auto* controlsRow = new QHBoxLayout();
    search_box_ = new QLineEdit();
    search_box_->setObjectName("searchBox");
    search_box_->setPlaceholderText("Поиск игр…");
    controlsRow->addWidget(search_box_);

    auto* refreshBtn = new QPushButton("Обновить");
    refreshBtn->setObjectName("sidebarButton");
    refreshBtn->setFixedWidth(100);
    connect(refreshBtn, &QPushButton::clicked, this, &GameListWidget::onRefresh);
    controlsRow->addWidget(refreshBtn);
    main_layout->addLayout(controlsRow);

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

void GameListWidget::onRefresh() {
    detector_.scanInstalledGames();
    detector_.detectRunningGames();
    buildGameCards();
    updateStatus();
}

void GameListWidget::buildGameCards() {
    games_.clear();
    game_cards_.clear();

    // clear grid
    QLayoutItem* item;
    while ((item = grid_layout_->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    // installed games from Steam/Epic/GOG first
    auto installed = detector_.getInstalledGames();
    QSet<QString> installedNames;
    for (const auto& g : installed) {
        GameInfo info;
        info.name = QString::fromStdString(g.name);
        info.category = translateCategory(QString::fromStdString(g.category));
        info.installed = true;
        info.running = g.is_running;
        info.iconColor = categoryColor(info.category);
        installedNames.insert(info.name);
        games_.append(info);
    }

    // supported games (known library) not already listed
    auto supported = detector_.getSupportedGames();
    for (const auto& g : supported) {
        GameInfo info;
        info.name = QString::fromStdString(g.name);
        if (installedNames.contains(info.name)) continue;
        info.category = translateCategory(QString::fromStdString(g.category));
        info.installed = false;
        info.running = g.is_running;
        info.iconColor = categoryColor(info.category);
        games_.append(info);
    }

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
            status_text = "● Запущена";
            status_color = "#00e676";
        } else if (game.installed) {
            status_text = "● Установлена";
            status_color = "#00bcd4";
        } else {
            status_text = "● Не установлена";
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
        QString("Показано игр: %1  |  установлено: %2  |  запущено: %3")
            .arg(total).arg(installed).arg(running));
}