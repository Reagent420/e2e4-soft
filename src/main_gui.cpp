#include <QApplication>
#include <QMessageBox>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <cstdio>

#include "ui/main_window.h"
#include "ui/system_tray.h"
#include "ui/theme.h"

void myMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg) {
    QByteArray localMsg = msg.toLocal8Bit();
    const char* typeStr = "";
    switch (type) {
        case QtDebugMsg:    typeStr = "DEBUG"; break;
        case QtInfoMsg:     typeStr = "INFO"; break;
        case QtWarningMsg:  typeStr = "WARN"; break;
        case QtCriticalMsg: typeStr = "CRIT"; break;
        case QtFatalMsg:    typeStr = "FATAL"; break;
    }
    fprintf(stderr, "[QT %s] %s (%s:%u %s)\n", typeStr, localMsg.constData(),
            context.file ? context.file : "", context.line, context.function ? context.function : "");
}

int main(int argc, char* argv[]) {
    qInstallMessageHandler(myMessageHandler);

    fprintf(stderr, "=== GNO v1.0.0 Starting ===\n");
    fprintf(stderr, "App dir: %s\n", QDir::currentPath().toUtf8().constData());

    QApplication app(argc, argv);
    app.setApplicationName("GNO");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("GNO");

    fprintf(stderr, "Platform: %s\n", QApplication::platformName().toUtf8().constData());

    QString appDir = QApplication::applicationDirPath();
    QStringList checks = {
        "platforms/qwindows.dll",
        "Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll"
    };
    bool allOk = true;
    for (const QString& f : checks) {
        bool ok = QFile::exists(appDir + "/" + f);
        fprintf(stderr, "Check %s: %s\n", f.toUtf8().constData(), ok ? "OK" : "MISSING!");
        if (!ok) allOk = false;
    }

    if (!allOk) {
        QMessageBox::critical(nullptr, "GNO Error",
            "Missing required files!\nMake sure all DLLs are in the same folder as GNO.exe.");
        return 1;
    }

    fprintf(stderr, "Applying stylesheet...\n");
    QSettings settings;
    bool darkTheme = settings.value("theme", "dark").toString() != "light";
    app.setStyleSheet(gno::theme::globalStyleSheet(darkTheme));

    fprintf(stderr, "Creating MainWindow...\n");
    try {
        gno::MainWindow window;
        gno::SystemTray tray;

        QObject::connect(&window, &gno::MainWindow::themeChanged,
                         &window, [&app, &settings](bool dark) {
            settings.setValue("theme", dark ? "dark" : "light");
            app.setStyleSheet(gno::theme::globalStyleSheet(dark));
        });
        QObject::connect(&tray, &gno::SystemTray::showRequested,
                         &window, &gno::MainWindow::forceShow);
        QObject::connect(&tray, &gno::SystemTray::boostToggled,
                         &window, [](bool on) {
            fprintf(stderr, "Boost toggled: %s\n", on ? "ON" : "OFF");
        });
        QObject::connect(&tray, &gno::SystemTray::quitRequested,
                         &app, &QApplication::quit);

        fprintf(stderr, "MainWindow + SystemTray created, showing...\n");
        window.show();
        fprintf(stderr, "=== Entering event loop ===\n");
        return app.exec();
    } catch (const std::exception& e) {
        fprintf(stderr, "FATAL: %s\n", e.what());
        QMessageBox::critical(nullptr, "GNO Fatal Error", QString("Exception: %1").arg(e.what()));
        return 1;
    } catch (...) {
        fprintf(stderr, "FATAL: unknown exception\n");
        QMessageBox::critical(nullptr, "GNO Fatal Error", "Unknown fatal error");
        return 1;
    }
}
