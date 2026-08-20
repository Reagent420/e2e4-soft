#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "ui/main_window.h"
#include "ui/monitoring.h"
#include "ui/sidebar.h"
#include "ui/dashboard.h"
#include "ui/network_tools.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCoreApplication>
#include <QPointer>
#include <QPushButton>
#include <QStackedWidget>

#include <array>
#include <chrono>
#include <thread>

int main(int argc, char** argv) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);
    return doctest::Context(argc, argv).run();
}

TEST_CASE("diagnostic sidebar has exactly six ordered buttons") {
    CHECK(static_cast<int>(gno::NavPage::Dashboard) == 0);
    CHECK(static_cast<int>(gno::NavPage::Diagnostics) == 3);
    CHECK(static_cast<int>(gno::NavPage::Count) == 6);

    gno::Sidebar sidebar;
    auto* group = sidebar.findChild<QButtonGroup*>();
    REQUIRE(group != nullptr);

    const std::array<QString, 6> labels = {
        QString::fromUtf8("Главная"),
        QString::fromUtf8("Игры"),
        QString::fromUtf8("Мониторинг"),
        QString::fromUtf8("Диагностика маршрута"),
        QString::fromUtf8("История"),
        QString::fromUtf8("Настройки")
    };
    CHECK(group->buttons().size() == static_cast<int>(labels.size()));
    for (int index = 0; index < static_cast<int>(labels.size()); ++index) {
        auto* button = group->button(index);
        REQUIRE(button != nullptr);
        CHECK(group->id(button) == index);
        CHECK(button->text() == labels[static_cast<std::size_t>(index)]);
    }
}

TEST_CASE("main window stack follows diagnostic navigation contract") {
    gno::MainWindow window(nullptr, false);
    auto* stack = window.findChild<QStackedWidget*>("mainStack");
    REQUIRE(stack != nullptr);
    REQUIRE(stack->count() == 6);

    const std::array<QString, 6> types = {
        "gno::DashboardWidget",
        "GameListWidget",
        "gno::MonitoringWidget",
        "gno::NetworkToolsWidget",
        "gno::SessionHistoryWidget",
        "SettingsPageWidget"
    };
    const std::array<QString, 6> objectNames = {
        "dashboardPage",
        "gamesPage",
        "monitoringPage",
        "diagnosticsPage",
        "historyPage",
        "settingsPage"
    };
    for (int index = 0; index < stack->count(); ++index) {
        CHECK(stack->widget(index)->objectName() == objectNames[static_cast<std::size_t>(index)]);
        CHECK(QString::fromLatin1(stack->widget(index)->metaObject()->className()) ==
              types[static_cast<std::size_t>(index)]);
    }
}

TEST_CASE("monitoring widget tears down without background monitoring") {
    auto* widget = new gno::MonitoringWidget(nullptr, false);
    QPointer<gno::MonitoringWidget> owner(widget);
    delete widget;
    CHECK(owner.isNull());
}

#ifndef PLATFORM_WINDOWS
TEST_CASE("unsupported diagnostic controls explain the platform limitation") {
    gno::DashboardWidget dashboard(nullptr, false);
    gno::NetworkToolsWidget tools;
    gno::MonitoringWidget monitoring(nullptr, true);

    const auto unavailable = QString::fromUtf8("Недоступно на этой платформе");
    bool dashboard_label_found = false;
    for (const auto* label : dashboard.findChildren<QLabel*>()) {
        dashboard_label_found = dashboard_label_found || label->text().contains(unavailable);
    }
    bool tools_label_found = false;
    for (const auto* label : tools.findChildren<QLabel*>()) {
        tools_label_found = tools_label_found || label->text().contains(unavailable);
    }
    bool monitoring_label_found = false;
    for (const auto* label : monitoring.findChildren<QLabel*>()) {
        monitoring_label_found = monitoring_label_found || label->text().contains(unavailable);
        CHECK_FALSE(label->text().contains(QString::fromUtf8("Тайм-аут — пакет потерян")));
    }

    CHECK(dashboard_label_found);
    CHECK(tools_label_found);
    CHECK(monitoring_label_found);
}
#endif

#ifndef PLATFORM_WINDOWS
TEST_CASE("monitoring widget joins its worker and drops queued updates on destruction") {
    auto* widget = new gno::MonitoringWidget(nullptr, true);
    QPointer<gno::MonitoringWidget> owner(widget);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    delete widget;
    QCoreApplication::processEvents();
    CHECK(owner.isNull());
}
#endif
