#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QScrollArea>

#include <QVector>

class QColorDialog;
class QHBoxLayout;

namespace gno {

class ThemeEditorWidget : public QWidget {
    Q_OBJECT
public:
    explicit ThemeEditorWidget(QWidget* parent = nullptr);

signals:
    void themeChanged();

private slots:
    void onFontSizeChanged(int val);
    void onApply();
    void onReset();

private:
    void setupUI();
    // Живой предпросмотр: образец карточки, кнопки и статусных плашек,
    // перекрашиваемый прямо во время выбора цвета. Без него подобрать палитру
    // приходилось вслепую: кнопка-образец показывает один цвет, а как он
    // смотрится рядом с текстом и фоном — не видно, пока не нажмёшь
    // «Применить» и не посмотришь на всё приложение.
    void setupPreview(QHBoxLayout* row);
    void refreshPreview();
    // Текущий выбранный цвет по ключу настройки (ещё не сохранённый в QSettings).
    QString currentColor(const QString& settings_key) const;

    void refreshColorButtons();
    static void paintColorButton(QPushButton* btn, const QString& color);

    struct ColorEntry {
        QLabel* label = nullptr;
        QPushButton* picker_btn = nullptr;
        QString settings_key;
        QString display_name;
        QString default_value;
    };
    QVector<ColorEntry> m_entries;
    QSpinBox* m_font_spin_ = nullptr;
    QPushButton* m_apply_btn_ = nullptr;
    QPushButton* m_reset_btn_ = nullptr;

    // Элементы предпросмотра.
    QWidget* m_preview_root_ = nullptr;
    QWidget* m_preview_card_ = nullptr;
    QLabel* m_preview_title_ = nullptr;
    QLabel* m_preview_subtitle_ = nullptr;
    QLabel* m_preview_accent_ = nullptr;
    QPushButton* m_preview_btn_ = nullptr;
    QLabel* m_preview_chip_ok_ = nullptr;
    QLabel* m_preview_chip_warn_ = nullptr;
    QLabel* m_preview_chip_err_ = nullptr;
};

} // namespace gno
