#pragma once

#include <QString>
#include <QColor>

namespace gno {
namespace theme {

constexpr const char* APP_NAME = "GNO - Game Network Optimizer";
constexpr const char* APP_VERSION = "1.0.0";

struct Colors {
    static constexpr auto BG_PRIMARY       = "#0D0D0F";
    static constexpr auto BG_SURFACE       = "#1A1A2E";
    static constexpr auto BG_ELEVATED      = "#252540";
    static constexpr auto BG_HOVER         = "#2E2E4A";
    static constexpr auto ACCENT_BLUE      = "#3B82F6";
    static constexpr auto ACCENT_CYAN      = "#06B6D4";
    static constexpr auto ACCENT_VIOLET    = "#7C3AED";
    static constexpr auto SUCCESS          = "#22C55E";
    static constexpr auto WARNING          = "#F59E0B";
    static constexpr auto ERROR            = "#EF4444";
    static constexpr auto TEXT_PRIMARY     = "#F8FAFC";
    static constexpr auto TEXT_SECONDARY   = "#94A3B8";
    static constexpr auto TEXT_TERTIARY    = "#64748B";
    static constexpr auto BORDER           = "rgba(255,255,255,0.08)";
    static constexpr auto BORDER_ACTIVE    = "rgba(59,130,246,0.5)";
    static constexpr auto GLOW_BLUE        = "rgba(59,130,246,0.3)";
    static constexpr auto GLOW_GREEN       = "rgba(34,197,94,0.3)";
};

inline QString globalStyleSheet() {
    return QStringLiteral(R"(
        * {
            font-family: "Segoe UI", "Inter", "SF Pro Display", system-ui, sans-serif;
        }

        QWidget {
            background-color: %1;
            color: %2;
            font-size: 13px;
        }

        QMainWindow {
            background-color: %1;
        }

        /* Sidebar */
        #sidebar {
            background-color: %3;
            border-right: 1px solid %7;
            min-width: 220px;
            max-width: 220px;
        }

        #sidebarButton {
            background: transparent;
            color: %4;
            border: none;
            border-radius: 8px;
            padding: 10px 16px;
            text-align: left;
            font-size: 13px;
            font-weight: 500;
        }

        #sidebarButton:hover {
            background-color: %5;
            color: %2;
        }

        #sidebarButton:checked {
            background-color: rgba(59,130,246,0.15);
            color: %6;
            font-weight: 600;
        }

        #logoLabel {
            font-size: 18px;
            font-weight: 700;
            color: %6;
            padding: 8px 0px 4px 0px;
        }

        #versionLabel {
            font-size: 11px;
            color: %8;
            padding-bottom: 16px;
        }

        /* Cards */
        #metricCard {
            background-color: %3;
            border: 1px solid %7;
            border-radius: 12px;
            padding: 20px;
        }

        #metricCard:hover {
            border-color: rgba(255,255,255,0.15);
        }

        /* Metric labels */
        #metricLabel {
            font-size: 11px;
            font-weight: 600;
            color: %8;
            text-transform: uppercase;
            letter-spacing: 1px;
        }

        #metricValue {
            font-size: 36px;
            font-weight: 700;
            color: %2;
        }

        #metricValueGood {
            font-size: 36px;
            font-weight: 700;
            color: %9;
        }

        #metricValueWarn {
            font-size: 36px;
            font-weight: 700;
            color: %10;
        }

        #metricValueBad {
            font-size: 36px;
            font-weight: 700;
            color: %11;
        }

        #metricDelta {
            font-size: 12px;
            font-weight: 500;
        }

        /* Boost button */
        #boostButton {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                stop:0 %6, stop:1 #2563EB);
            color: white;
            border: none;
            border-radius: 12px;
            padding: 16px 32px;
            font-size: 16px;
            font-weight: 700;
            letter-spacing: 1px;
            min-height: 24px;
        }

        #boostButton:hover {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                stop:0 #4F8EF7, stop:1 #3B82F6);
        }

        #boostButton:pressed {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                stop:0 #2563EB, stop:1 #1D4ED8);
        }

        #boostButtonActive {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                stop:0 #16A34A, stop:1 #15803D);
            color: white;
            border: none;
            border-radius: 12px;
            padding: 16px 32px;
            font-size: 16px;
            font-weight: 700;
            letter-spacing: 1px;
            min-height: 24px;
        }

        #boostButtonActive:hover {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                stop:0 #22C55E, stop:1 #16A34A);
        }

        /* Game cards */
        #gameCard {
            background-color: %3;
            border: 1px solid %7;
            border-radius: 10px;
            padding: 12px;
        }

        #gameCard:hover {
            border-color: %6;
            background-color: %5;
        }

        #gameCardSelected {
            background-color: rgba(59,130,246,0.12);
            border: 1px solid %6;
            border-radius: 10px;
            padding: 12px;
        }

        #gameTitle {
            font-size: 13px;
            font-weight: 600;
            color: %2;
        }

        #gameCategory {
            font-size: 11px;
            color: %8;
        }

        /* Search */
        QLineEdit#searchBox {
            background-color: %3;
            border: 1px solid %7;
            border-radius: 8px;
            padding: 10px 14px;
            color: %2;
            font-size: 13px;
            selection-background-color: %6;
        }

        QLineEdit#searchBox:focus {
            border-color: %6;
        }

        /* Scroll area */
        QScrollArea {
            border: none;
            background-color: transparent;
        }

        QScrollBar:vertical {
            background: transparent;
            width: 8px;
            margin: 0;
        }

        QScrollBar::handle:vertical {
            background: rgba(255,255,255,0.15);
            border-radius: 4px;
            min-height: 30px;
        }

        QScrollBar::handle:vertical:hover {
            background: rgba(255,255,255,0.25);
        }

        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0;
        }

        QScrollBar:horizontal {
            background: transparent;
            height: 8px;
            margin: 0;
        }

        QScrollBar::handle:horizontal {
            background: rgba(255,255,255,0.15);
            border-radius: 4px;
            min-width: 30px;
        }

        /* Status bar */
        #statusBar {
            background-color: %3;
            border-top: 1px solid %7;
            padding: 8px 16px;
        }

        #statusConnected {
            color: %9;
            font-weight: 600;
            font-size: 12px;
        }

        #statusDisconnected {
            color: %8;
            font-weight: 500;
            font-size: 12px;
        }

        /* Section headers */
        #sectionTitle {
            font-size: 16px;
            font-weight: 700;
            color: %2;
            padding: 4px 0;
        }

        #sectionSubtitle {
            font-size: 12px;
            color: %8;
            padding-bottom: 8px;
        }

        /* Settings */
        #settingsGroup {
            background-color: %3;
            border: 1px solid %7;
            border-radius: 10px;
            padding: 16px;
        }

        #settingsLabel {
            font-size: 13px;
            font-weight: 600;
            color: %2;
        }

        #settingsDescription {
            font-size: 12px;
            color: %8;
        }

        QCheckBox {
            spacing: 8px;
            color: %2;
            font-size: 13px;
        }

        QCheckBox::indicator {
            width: 18px;
            height: 18px;
            border-radius: 4px;
            border: 2px solid %7;
            background-color: transparent;
        }

        QCheckBox::indicator:checked {
            background-color: %6;
            border-color: %6;
        }

        QComboBox {
            background-color: %3;
            border: 1px solid %7;
            border-radius: 6px;
            padding: 8px 12px;
            color: %2;
            font-size: 13px;
            min-width: 100px;
        }

        QComboBox::drop-down {
            border: none;
            width: 24px;
        }

        QComboBox QAbstractItemView {
            background-color: %3;
            border: 1px solid %7;
            border-radius: 6px;
            selection-background-color: %5;
            color: %2;
            padding: 4px;
        }

        /* Tab widget */
        QTabWidget::pane {
            border: none;
            background-color: transparent;
        }

        QTabBar::tab {
            background-color: transparent;
            color: %8;
            padding: 10px 20px;
            font-size: 13px;
            font-weight: 500;
            border-bottom: 2px solid transparent;
        }

        QTabBar::tab:selected {
            color: %6;
            border-bottom-color: %6;
        }

        QTabBar::tab:hover {
            color: %2;
        }

        /* Tooltip */
        QToolTip {
            background-color: %3;
            color: %2;
            border: 1px solid %7;
            border-radius: 6px;
            padding: 6px 10px;
            font-size: 12px;
        }
    )")
    .arg(Colors::BG_PRIMARY)        // %1
    .arg(Colors::TEXT_PRIMARY)      // %2
    .arg(Colors::BG_SURFACE)        // %3
    .arg(Colors::TEXT_SECONDARY)    // %4
    .arg(Colors::BG_HOVER)          // %5
    .arg(Colors::ACCENT_BLUE)       // %6
    .arg(Colors::BORDER)            // %7
    .arg(Colors::TEXT_TERTIARY)     // %8
    .arg(Colors::SUCCESS)           // %9
    .arg(Colors::WARNING)           // %10
    .arg(Colors::ERROR);            // %11
}

} // namespace theme
} // namespace gno
