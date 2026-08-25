#include "theme_editor.h"
#include "theme.h"

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
        auto* btn = new QPushButton(saved, colors_widget);
        btn->setFixedHeight(30);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(QString(
            "QPushButton { background-color: %1; color: %2;"
            " border: 1px solid %3; border-radius: 6px;"
            " padding: 4px 12px; font-family: Consolas; }")
                .arg(saved,
                     QColor(saved).lightness() > 128 ? "#000" : "#FFF",
                     theme::Colors::BORDER));
        connect(btn, &QPushButton::clicked, this, [this, btn, i]() {
            QColor current(btn->text());
            QColor picked = QColorDialog::getColor(current, this,
                kEditableColors[i].display_ru);
            if (!picked.isValid()) return;
            btn->setText(picked.name());
            btn->setStyleSheet(QString(
                "QPushButton { background-color: %1; color: %2;"
                " border: 1px solid %3; border-radius: 6px;"
                " padding: 4px 12px; font-family: Consolas; }")
                    .arg(picked.name(),
                         picked.lightness() > 128 ? "#000" : "#FFF",
                         theme::Colors::BORDER));
        });

        form->addRow(lbl, btn);

        ColorEntry entry;
        entry.label = lbl;
        entry.picker_btn = btn;
        entry.settings_key = def.settings_key;
        entry.display_name = def.display_ru;
        m_entries.push_back(entry);
    }
    scroll->setWidget(colors_widget);
    root->addWidget(scroll, 1);

    // Font size
    auto* fontRow = new QHBoxLayout();
    fontRow->addWidget(new QLabel("Размер шрифта", this));
    m_font_spin_ = new QSpinBox(this);
    m_font_spin_->setRange(10, 18);
    m_font_spin_->setValue(s.value("app/fontSize", 13).toInt());
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

void ThemeEditorWidget::refreshColorButtons() {
    QSettings s;
    for (const auto& entry : m_entries) {
        QString saved = s.value(entry.settings_key, "").toString();
        if (!saved.isEmpty()) entry.picker_btn->setText(saved);
    }
}

void ThemeEditorWidget::onFontSizeChanged(int val) {
    QSettings().setValue("app/fontSize", val);
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
    s.remove("theme/");
    emit themeChanged();
}

} // namespace gno
