#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QScrollArea>

#include <QVector>

class QColorDialog;

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
    void refreshColorButtons();

    struct ColorEntry {
        QLabel* label = nullptr;
        QPushButton* picker_btn = nullptr;
        QString settings_key;
        QString display_name;
    };
    QVector<ColorEntry> m_entries;
    QSpinBox* m_font_spin_ = nullptr;
    QPushButton* m_apply_btn_ = nullptr;
    QPushButton* m_reset_btn_ = nullptr;
};

} // namespace gno
