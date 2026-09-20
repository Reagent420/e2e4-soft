#include "theme_editor.h"
#include "theme.h"
#include "core/settings_keys.h"

#include <QApplication>
#include <QBrush>
#include <QColorDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSettings>
#include <QTableWidget>

#include <algorithm>

namespace gno {

namespace {

struct ColorDef {
    const char* settings_key;
    const char* display_ru;
    const char* default_value;
};

const ColorDef kEditableColors[] = {
    {"theme/bg_primary",     "Фон приложения",   "#0A0E17"},
    {"theme/bg_surface",     "Фон карточек",     "#101A2B"},
    {"theme/accent_neon",    "Акцентный цвет",   "#00F0FF"},
    {"theme/accent_sky",     "Цвет кнопок",      "#0EA5E9"},
    {"theme/text_primary",   "Текст основной",   "#EAF6FF"},
    {"theme/text_secondary", "Текст вторичный",  "#9DB8D6"},
    {"theme/success",        "Успех",            "#39FF14"},
    {"theme/warning",        "Предупреждение",   "#FFB000"},
    {"theme/error",          "Ошибка",           "#FF2E88"},
};

constexpr int kColorCount = sizeof(kEditableColors) / sizeof(kEditableColors[0]);

} // namespace

ThemeEditorWidget::ThemeEditorWidget(QWidget* parent) : QWidget(parent) { setupUI(); }

void ThemeEditorWidget::setupUI() {
    setObjectName("themeEditorPage");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 20, 24, 20);
    root->setSpacing(12);

    // Title
    auto* title = new QLabel("Редактор оформления", this);
    title->setStyleSheet(QString(
        "font-size:17px; font-weight:700; letter-spacing:3px;"
        "color:%1; background:transparent;").arg(theme::Colors::ACCENT_NEON));

    auto* subtitle = new QLabel(
        "Настройка цветов и шрифта приложения. Изменения применяются мгновенно.",
        this);
    subtitle->setWordWrap(true);
    subtitle->setStyleSheet(QString(
        "color:%1; background:transparent;").arg(theme::Colors::TEXT_SECONDARY));

    root->addWidget(title);
    root->addWidget(subtitle);

    // Color pickers in scroll area
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* colors_widget = new QWidget();
    auto* form = new QFormLayout(colors_widget);
    form->setSpacing(6);

    QSettings s;
    for (int i = 0; i < kColorCount; ++i) {
        const auto& def = kEditableColors[i];
        QString saved = s.value(def.settings_key, def.default_value).toString();

        auto* lbl = new QLabel(def.display_ru, colors_widget);
        auto* btn = new QPushButton(colors_widget);
        btn->setFixedHeight(30);
        btn->setCursor(Qt::PointingHandCursor);
        paintColorButton(btn, saved);
        connect(btn, &QPushButton::clicked, this, [this, btn, i]() {
            QColor current(btn->text());
            QColor picked = QColorDialog::getColor(current, this,
                kEditableColors[i].display_ru);
            if (!picked.isValid()) return;
            paintColorButton(btn, picked.name());
            refreshPreview();   // предпросмотр обновляем сразу, без «Применить»
        });

        form->addRow(lbl, btn);

        ColorEntry entry;
        entry.label = lbl;
        entry.picker_btn = btn;
        entry.settings_key = def.settings_key;
        entry.display_name = def.display_ru;
        entry.default_value = QString::fromLatin1(def.default_value);
        m_entries.push_back(entry);
    }
    scroll->setWidget(colors_widget);

    // Список цветов слева, предпросмотр справа.
    auto* contentRow = new QHBoxLayout();
    contentRow->addWidget(scroll, 1);
    setupPreview(contentRow);
    root->addLayout(contentRow, 1);

    // Font size
    auto* fontRow = new QHBoxLayout();
    fontRow->addWidget(new QLabel("Размер шрифта", this));
    m_font_spin_ = new QSpinBox(this);
    m_font_spin_->setRange(10, 18);
    m_font_spin_->setValue(s.value(SettingsKeys::FontSize(), 13).toInt());
    connect(m_font_spin_, &QSpinBox::valueChanged, this,
            &ThemeEditorWidget::onFontSizeChanged);
    fontRow->addWidget(m_font_spin_);
    fontRow->addStretch();
    root->addLayout(fontRow);

    // Apply / Reset buttons
    auto* btnRow = new QHBoxLayout();
    m_apply_btn_ = new QPushButton("Применить тему", this);
    m_apply_btn_->setObjectName("boostButton");
    m_reset_btn_ = new QPushButton("Сбросить к стандартным", this);
    connect(m_apply_btn_, &QPushButton::clicked, this, &ThemeEditorWidget::onApply);
    connect(m_reset_btn_, &QPushButton::clicked, this, &ThemeEditorWidget::onReset);
    btnRow->addWidget(m_apply_btn_);
    btnRow->addWidget(m_reset_btn_);
    btnRow->addStretch();
    root->addLayout(btnRow);
}

void ThemeEditorWidget::setupPreview(QHBoxLayout* row) {
    m_preview_root_ = new QWidget(this);
    m_preview_root_->setFixedWidth(290);
    auto* outer = new QVBoxLayout(m_preview_root_);
    outer->setContentsMargins(14, 0, 0, 0);
    outer->setSpacing(8);

    auto* caption = new QLabel("Предпросмотр", m_preview_root_);
    caption->setStyleSheet(QString("color:%1; background:transparent; font-weight:600;")
                               .arg(theme::Colors::TEXT_SECONDARY));
    outer->addWidget(caption);

    m_preview_card_ = new QWidget(m_preview_root_);
    auto* card = new QVBoxLayout(m_preview_card_);
    card->setContentsMargins(16, 14, 16, 14);
    card->setSpacing(8);

    m_preview_title_ = new QLabel("Заголовок карточки", m_preview_card_);
    m_preview_subtitle_ = new QLabel(
        "Вторичный текст: так выглядит пояснение к метрике.", m_preview_card_);
    m_preview_subtitle_->setWordWrap(true);
    m_preview_accent_ = new QLabel("АКЦЕНТ", m_preview_card_);

    m_preview_btn_ = new QPushButton("Кнопка действия", m_preview_card_);
    m_preview_btn_->setCursor(Qt::PointingHandCursor);
    m_preview_btn_->setMinimumHeight(30);

    auto* chips = new QHBoxLayout();
    chips->setSpacing(6);
    m_preview_chip_ok_ = new QLabel("Норма", m_preview_card_);
    m_preview_chip_warn_ = new QLabel("Внимание", m_preview_card_);
    m_preview_chip_err_ = new QLabel("Ошибка", m_preview_card_);
    chips->addWidget(m_preview_chip_ok_);
    chips->addWidget(m_preview_chip_warn_);
    chips->addWidget(m_preview_chip_err_);
    chips->addStretch();

    card->addWidget(m_preview_title_);
    card->addWidget(m_preview_subtitle_);
    card->addWidget(m_preview_accent_);
    card->addWidget(m_preview_btn_, 0, Qt::AlignLeft);
    card->addLayout(chips);

    outer->addWidget(m_preview_card_);
    outer->addStretch();

    row->addWidget(m_preview_root_);
    refreshPreview();
}

QString ThemeEditorWidget::currentColor(const QString& settings_key) const {
    for (const auto& entry : m_entries) {
        if (entry.settings_key == settings_key && entry.picker_btn)
            return entry.picker_btn->text();
    }
    return QString();
}

void ThemeEditorWidget::refreshPreview() {
    if (!m_preview_root_) return;

    // Читаем цвета прямо с кнопок, а не из QSettings: предпросмотр должен
    // реагировать на выбор до нажатия «Применить тему».
    const QString bgPrimary = currentColor("theme/bg_primary");
    const QString bgSurface = currentColor("theme/bg_surface");
    const QString accentNeon = currentColor("theme/accent_neon");
    const QString accentSky = currentColor("theme/accent_sky");
    const QString textPrimary = currentColor("theme/text_primary");
    const QString textSecondary = currentColor("theme/text_secondary");
    const QString success = currentColor("theme/success");
    const QString warning = currentColor("theme/warning");
    const QString error = currentColor("theme/error");

    // Тёмный или светлый текст на плашке — по яркости фона, иначе надпись
    // сливается с цветом.
    auto onColor = [](const QString& bg) {
        return QColor(bg).lightness() > 128 ? QStringLiteral("#000000")
                                            : QStringLiteral("#FFFFFF");
    };
    auto chipStyle = [&onColor](const QString& bg) {
        return QString("background-color:%1; color:%2; border-radius:10px;"
                       " padding:3px 10px; font-size:12px;")
            .arg(bg, onColor(bg));
    };

    m_preview_root_->setStyleSheet(
        QString("background-color:%1; border-radius:8px;").arg(bgPrimary));
    m_preview_card_->setStyleSheet(
        QString("background-color:%1; border:1px solid %2; border-radius:10px;")
            .arg(bgSurface, theme::Colors::BORDER));
    m_preview_title_->setStyleSheet(
        QString("color:%1; font-size:14px; font-weight:700; background:transparent;")
            .arg(textPrimary));
    m_preview_subtitle_->setStyleSheet(
        QString("color:%1; font-size:12px; background:transparent;").arg(textSecondary));
    m_preview_accent_->setStyleSheet(
        QString("color:%1; font-size:11px; font-weight:700; letter-spacing:2px;"
                " background:transparent;").arg(accentNeon));
    m_preview_btn_->setStyleSheet(
        QString("QPushButton { background-color:%1; color:%2; border:none;"
                " border-radius:6px; padding:6px 14px; font-size:12px; }")
            .arg(accentSky, onColor(accentSky)));
    m_preview_chip_ok_->setStyleSheet(chipStyle(success));
    m_preview_chip_warn_->setStyleSheet(chipStyle(warning));
    m_preview_chip_err_->setStyleSheet(chipStyle(error));
}

void ThemeEditorWidget::paintColorButton(QPushButton* btn, const QString& color) {
    btn->setText(color);
    btn->setStyleSheet(QString(
        "QPushButton { background-color: %1; color: %2;"
        " border: 1px solid %3; border-radius: 6px;"
        " padding: 4px 12px; font-family: Consolas; }")
            .arg(color,
                 QColor(color).lightness() > 128 ? "#000" : "#FFF",
                 theme::Colors::BORDER));
}

void ThemeEditorWidget::refreshColorButtons() {
    QSettings s;
    for (const auto& entry : m_entries) {
        // Берём именно стандартный цвет как fallback: после удаления ключа
        // (onReset) settings_value() вернёт пустую строку, и без правильного
        // умолчания кнопка не обновится.
        QString saved = s.value(entry.settings_key, entry.default_value).toString();
        paintColorButton(entry.picker_btn, saved);
    }
    refreshPreview();
}

void ThemeEditorWidget::onFontSizeChanged(int val) {
    QSettings().setValue(SettingsKeys::FontSize(), val);
    QFont f = QApplication::font();
    f.setPointSize(val);
    QApplication::setFont(f);
}

void ThemeEditorWidget::onApply() {
    QSettings s;
    for (const auto& entry : m_entries) {
        s.setValue(entry.settings_key, entry.picker_btn->text());
    }
    emit themeChanged();
}

void ThemeEditorWidget::onReset() {
    QSettings s;
    // QSettings::remove() не умеет удалять «группу» по префиксу «theme/» —
    // такой аргумент трактуется как литеральный ключ, которого не существует.
    // Старая реализация поэтому ничего не сбрасывала: пользовательские цвета
    // оставались в реестре после «Сбросить к стандартным». Удаляем каждый ключ
    // явно, потом перерисовываем кнопки и спиннет шрифта.
    for (int i = 0; i < kColorCount; ++i) {
        s.remove(kEditableColors[i].settings_key);
    }
    s.remove(SettingsKeys::FontSize());
    refreshColorButtons();
    m_font_spin_->setValue(s.value(SettingsKeys::FontSize(), 13).toInt());
    emit themeChanged();
}

} // namespace gno
