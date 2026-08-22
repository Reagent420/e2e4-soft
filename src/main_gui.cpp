#include <QApplication>
#include <QMessageBox>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QObject>
#include <cstdio>

#include "ui/main_window.h"
#include "ui/system_tray.h"
#include "ui/overlay.h"
#include "ui/theme.h"
#include "monitoring/monitoring_service.h"
#include "optimization/fps_optimizer.h"
#include "core/network_utils.h"

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

static gno::FPSBoostConfig loadBoostConfig() {
    QSettings settings;
    gno::FPSBoostConfig cfg;
    cfg.disable_game_dvr = settings.value("optimizer/gameDvr", true).toBool();
    cfg.disable_fullscreen_optimizations = settings.value("optimizer/fullscreenOpt", true).toBool();
    cfg.disable_mouse_acceleration = settings.value("optimizer/mouseAccel", true).toBool();
    cfg.disable_game_mode = settings.value("optimizer/gameMode", false).toBool();
    cfg.optimize_power_plan = settings.value("optimizer/powerPlan", true).toBool();
    cfg.set_high_priority = settings.value("optimizer/highPriority", true).toBool();
    cfg.optimize_virtual_memory = settings.value("optimizer/virtualMemory", true).toBool();
    return cfg;
}

static void applyBoost(bool on, gno::MainWindow& window) {
    gno::MonitoringService& svc = gno::MonitoringService::instance();
    svc.setBoostActive(on);
    window.setBoostIndicator(on);

    if (on) {
        gno::FPSOptimizer optimizer;
        auto result = optimizer.applyConfig(loadBoostConfig());
        if (!result.warnings.empty())
            window.showRecommendation(QString::fromStdString(result.warnings[0]));

        QSettings settings;
        if (settings.value("optimizer/tcpOpt", true).toBool())
            gno::NetworkUtils::applyTCPOptimizations(true);
        if (settings.value("optimizer/mtuOpt", true).toBool()) {
            std::string iface = gno::NetworkUtils::getNetworkInterfaceName();
            if (iface != "default")
                gno::NetworkUtils::setMTU(iface, settings.value("optimizer/mtuValue", 1400).toUInt());
        }
        if (settings.value("optimizer/customDns", false).toBool()) {
            std::string iface = gno::NetworkUtils::getNetworkInterfaceName();
            if (iface != "default")
                gno::NetworkUtils::setDNS(iface, settings.value("optimizer/dnsServer", "1.1.1.1").toString().toStdString());
        }
        fprintf(stderr, "Boost applied\n");
    } else {
        gno::FPSOptimizer optimizer;
        optimizer.revertAll();
        gno::NetworkUtils::applyTCPOptimizations(false);
        fprintf(stderr, "Boost reverted\n");
    }
}

int main(int argc, char* argv[]) {
    qInstallMessageHandler(myMessageHandler);

    fprintf(stderr, "=== E2E4 Soft v1.6.0 Starting ===\n");
    fprintf(stderr, "App dir: %s\n", QDir::currentPath().toUtf8().constData());

    QApplication app(argc, argv);
    app.setApplicationName("GNO");
    app.setApplicationVersion("1.6.0");
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
        QMessageBox::critical(nullptr, QString::fromUtf8("Ошибка E2E4 Soft"),
            QString::fromUtf8("Отсутствуют необходимые файлы!\nУбедитесь, что все DLL находятся в одной папке с E2E4.exe."));
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

        gno::MonitoringService& svc = gno::MonitoringService::instance();
        svc.setSoundEnabled(settings.value("soundNotifications", true).toBool());
        svc.start();

        gno::OverlayWidget overlay;
        overlay.setCorner(static_cast<gno::OverlayWidget::Corner>(
            settings.value("overlayCorner", 1).toInt()));
        overlay.setOpacityPercent(
            QList<int>{60, 75, 85, 95, 100}.value(settings.value("overlayOpacity", 2).toInt(), 85));
        if (settings.value("overlayEnabled", false).toBool())
            overlay.showOverlay();

        // settings page lives inside the main window; forward its signals
        QObject::connect(&window, &gno::MainWindow::themeChanged,
                &window, [&app, &settings](bool dark) {
            settings.setValue("theme", dark ? "dark" : "light");
            app.setStyleSheet(gno::theme::globalStyleSheet(dark));
        });

        QObject::connect(&tray, &gno::SystemTray::showRequested,
                &window, &gno::MainWindow::forceShow);

        QObject::connect(&tray, &gno::SystemTray::boostToggled,
                &window, [&window](bool on) {
            applyBoost(on, window);
        });

        QObject::connect(&tray, &gno::SystemTray::overlayToggled,
                &overlay, &gno::OverlayWidget::toggleOverlay);

        QObject::connect(&tray, &gno::SystemTray::quitRequested,
                &app, &QApplication::quit);

        // service -> tray
        QObject::connect(&svc, &gno::MonitoringService::pingUpdated,
                &tray, &gno::SystemTray::updatePing);
        QObject::connect(&svc, &gno::MonitoringService::jitterUpdated,
                &tray, &gno::SystemTray::updateJitter);
        QObject::connect(&svc, &gno::MonitoringService::lossUpdated,
                &tray, &gno::SystemTray::updatePacketLoss);

        // service -> status bar
        QObject::connect(&svc, &gno::MonitoringService::pingUpdated,
                &window, [&window](double ms) {
            const gno::MonitoringService& s = gno::MonitoringService::instance();
            window.updateLiveMetrics(static_cast<int>(ms), static_cast<int>(s.currentJitter()),
                                     s.currentLossPercent());
        });
        QObject::connect(&svc, &gno::MonitoringService::recommendationAvailable,
                &window, &gno::MainWindow::showRecommendation);

        // service -> notifications
        QObject::connect(&svc, &gno::MonitoringService::gameStarted,
                &tray, [&tray](const QString& game) {
            tray.showMessage(QString::fromUtf8("Игра запущена"),
                             QString::fromUtf8("%1 — записываем сессию и следим за пингом").arg(game));
        });
        QObject::connect(&svc, &gno::MonitoringService::gameEnded,
                &tray, [&tray](const QString& game) {
            tray.showMessage(QString::fromUtf8("Игра завершена"),
                             QString::fromUtf8("%1 — сессия сохранена в истории").arg(game));
        });

        // settings page -> overlay / service / tray
        QObject::connect(&window, &gno::MainWindow::overlaySettingsChanged,
                &overlay, [&overlay](bool enabled, int corner, int opacity) {
            QSettings s;
            s.setValue("overlayEnabled", enabled);
            s.setValue("overlayCorner", corner);
            s.setValue("overlayOpacity", opacity);
            overlay.setCorner(static_cast<gno::OverlayWidget::Corner>(corner));
            overlay.setOpacityPercent(QList<int>{60, 75, 85, 95, 100}.value(opacity, 85));
            if (enabled)
                overlay.showOverlay();
            else
                overlay.hideOverlay();
        });
        QObject::connect(&window, &gno::MainWindow::soundSettingsChanged,
                &svc, &gno::MonitoringService::setSoundEnabled);
        QObject::connect(&window, &gno::MainWindow::notificationsSettingsChanged,
                &tray, [&tray](bool enabled) {
            Q_UNUSED(enabled);
            tray.showMessage(QString::fromUtf8("Уведомления"),
                             enabled ? QString::fromUtf8("Уведомления включены")
                                     : QString::fromUtf8("Уведомления выключены"));
        });

        // overlay hotkey state sync: F9 inside the overlay also emits toggle via tray check state
        QObject::connect(&overlay, &gno::OverlayWidget::overlayToggled,
                &tray, &gno::SystemTray::setOverlayOn);

        fprintf(stderr, "MainWindow + SystemTray + MonitoringService created, showing...\n");
        window.show();
        fprintf(stderr, "=== Entering event loop ===\n");
        return app.exec();
    } catch (const std::exception& e) {
        fprintf(stderr, "FATAL: %s\n", e.what());
        QMessageBox::critical(nullptr, QString::fromUtf8("Критическая ошибка E2E4 Soft"), QString("Exception: %1").arg(e.what()));
        return 1;
    } catch (...) {
        fprintf(stderr, "FATAL: unknown exception\n");
        QMessageBox::critical(nullptr, QString::fromUtf8("Критическая ошибка E2E4 Soft"), QString::fromUtf8("Неизвестная ошибка"));
        return 1;
    }
}