#include <QApplication>
#include "ui/main_window.h"
#include <QStyleFactory>
#include <QDir>
#include <QStandardPaths>
#include <QMessageBox>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Game Network Optimizer");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("GNO");
    
    app.setStyle(QStyleFactory::create("Fusion"));
    
    QPalette dark_palette;
    dark_palette.setColor(QPalette::Window, QColor(53, 53, 53));
    dark_palette.setColor(QPalette::WindowText, Qt::white);
    dark_palette.setColor(QPalette::Base, QColor(25, 25, 25));
    dark_palette.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
    dark_palette.setColor(QPalette::ToolTipBase, Qt::white);
    dark_palette.setColor(QPalette::ToolTipText, Qt::white);
    dark_palette.setColor(QPalette::Text, Qt::white);
    dark_palette.setColor(QPalette::Button, QColor(53, 53, 53));
    dark_palette.setColor(QPalette::ButtonText, Qt::white);
    dark_palette.setColor(QPalette::BrightText, Qt::red);
    dark_palette.setColor(QPalette::Link, QColor(42, 130, 218));
    dark_palette.setColor(QPalette::Highlight, QColor(42, 130, 218));
    dark_palette.setColor(QPalette::HighlightedText, Qt::black);
    app.setPalette(dark_palette);
    
    QString styles = R"(
        QMainWindow {
            background-color: #353535;
        }
        QWidget {
            background-color: #353535;
            color: white;
            font-family: "Segoe UI", Arial, sans-serif;
        }
        QPushButton {
            background-color: #2a82da;
            border: none;
            padding: 8px 16px;
            border-radius: 4px;
            color: white;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: #3498db;
        }
        QPushButton:pressed {
            background-color: #2176ae;
        }
        QPushButton:disabled {
            background-color: #555;
            color: #888;
        }
        QLabel {
            color: white;
        }
        QLineEdit {
            background-color: #1a1a1a;
            border: 1px solid #555;
            padding: 6px;
            border-radius: 4px;
            color: white;
        }
        QLineEdit:focus {
            border: 1px solid #2a82da;
        }
        QComboBox {
            background-color: #1a1a1a;
            border: 1px solid #555;
            padding: 6px;
            border-radius: 4px;
            color: white;
        }
        QComboBox::drop-down {
            border: none;
        }
        QComboBox QAbstractItemView {
            background-color: #1a1a1a;
            selection-background-color: #2a82da;
            color: white;
        }
        QListWidget {
            background-color: #1a1a1a;
            border: 1px solid #555;
            border-radius: 4px;
            color: white;
        }
        QListWidget::item {
            padding: 8px;
        }
        QListWidget::item:selected {
            background-color: #2a82da;
        }
        QListWidget::item:hover {
            background-color: #2a82da40;
        }
        QGroupBox {
            border: 1px solid #555;
            border-radius: 6px;
            margin-top: 12px;
            padding-top: 12px;
            font-weight: bold;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 12px;
            padding: 0 6px;
        }
        QTabWidget::pane {
            border: 1px solid #555;
            border-radius: 4px;
        }
        QTabBar::tab {
            background-color: #2a2a2a;
            color: #aaa;
            padding: 8px 16px;
            border-top-left-radius: 4px;
            border-top-right-radius: 4px;
        }
        QTabBar::tab:selected {
            background-color: #353535;
            color: white;
        }
        QTabBar::tab:hover {
            background-color: #2a82da40;
        }
        QSpinBox {
            background-color: #1a1a1a;
            border: 1px solid #555;
            padding: 4px;
            border-radius: 4px;
            color: white;
        }
        QCheckBox {
            spacing: 8px;
        }
        QCheckBox::indicator {
            width: 16px;
            height: 16px;
        }
        QScrollBar:vertical {
            background-color: #1a1a1a;
            width: 10px;
            border-radius: 5px;
        }
        QScrollBar::handle:vertical {
            background-color: #555;
            border-radius: 5px;
            min-height: 20px;
        }
        QScrollBar::handle:vertical:hover {
            background-color: #2a82da;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0;
        }
    )";
    app.setStyleSheet(styles);
    
    try {
        gno::ui::MainWindow window;
        window.initialize();
        window.show();
        return app.exec();
    } catch (const std::exception& e) {
        QMessageBox::critical(nullptr, "Error", QString("Failed to initialize: %1").arg(e.what()));
        return 1;
    }
}
