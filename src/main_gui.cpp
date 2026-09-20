#include <QApplication>
#include <QMessageBox>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QObject>
#include <QTimer>
#include <string>

#include "ui/main_window.h"
#include "ui/system_tray.h"
#include "ui/overlay.h"
#include "ui/theme.h"
#include "ui/update_checker.h"
#include "monitoring/monitoring_service.h"
#include "core/alert_thresholds.h"
#include "core/settings_keys.h"
#include "core/logger.h"
#include "remediation/boost_gate.h"

#include <filesystem>

namespace gno_log = gno::log;

// Раньше все сообщения шли в stderr. При запуске из ярлыка (подсистема WIN32,
// без консоли) stderr уходит в никуда, поэтому лог теперь пишется в файл —
// см. core/logger.h. Источник (файл:строка) сохраняем: без него разбор
// жалоб по логу превращается в гадание.
void myMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg) {
    const char* typeStr = "";
    gno_log::Level level = gno_log::Level::Info;
    switch (type) {
        case QtDebugMsg:    typeStr = "DEBUG"; level = gno_log::Level::Debug; break;
        case QtInfoMsg:     typeStr = "INFO";  level = gno_log::Level::Info; break;
        case QtWarningMsg:  typeStr = "WARN";  level = gno_log::Level::Warning; break;
        case QtCriticalMsg: typeStr = "CRIT";  level = gno_log::Level::Error; break;
        case QtFatalMsg:    typeStr = "FATAL"; level = gno_log::Level::Critical; break;
    }

    std::string line = std::string("[QT ") + typeStr + "] " + msg.toStdString();
    if (context.file) {
        line += " (";
        line += context.file;
        line += ':';
        line += std::to_string(context.line);
        line += ')';
    }
    gno_log::write(level, line);
}

static void applyBoost(bool on, gno::MainWindow& window) {
    gno::MonitoringService& svc = gno::MonitoringService::instance();
    svc.setBoostActive(on);
    window.setBoostIndicator(on);

    namespace rem = gno::remediation;
    rem::BoostGate gate(rem::defaultBackupsDir());

    if (on) {
        QSettings settings;
        namespace sk = gno::SettingsKeys;
        rem::BoostRequest req;
        req.disable_game_dvr = settings.value(sk::GameDvr(), true).toBool();
        req.disable_fullscreen_optimizations = settings.value(sk::FullscreenOpt(), true).toBool();
        req.disable_mouse_acceleration = settings.value(sk::MouseAccel(), true).toBool();
        req.disable_game_mode = settings.value(sk::GameMode(), false).toBool();
        req.optimize_power_plan = settings.value(sk::PowerPlan(), true).toBool();
        req.set_high_priority = settings.value(sk::HighPriority(), true).toBool();
        req.optimize_virtual_memory = false; // engine-off-policy: skip with explanation
        req.tcp_optimization = settings.value(sk::TcpOpt(), true).toBool();
        req.mtu_optimization = settings.value(sk::MtuOpt(), true).toBool();
        req.mtu_value = settings.value(sk::MtuValue(), 1500).toUInt();
        req.custom_dns = settings.value(sk::CustomDns(), false).toBool();
        req.dns_server = settings.value(sk::DnsServer(), "1.1.1.1").toString().toStdString();

        auto result = gate.applyBoost(req);
        window.showRecommendation(QString::fromUtf8("Буст через безопасный движок: %1")
                                      .arg(QString::fromStdString(result.detail)));
        if (!result.skipped.empty())
            window.showRecommendation(QString::fromUtf8("Пропущено: %1")
                                          .arg(QString::fromStdString(result.skipped[0])));
        gno_log::write(gno_log::Level::Info,
                       std::string("Boost applied via gate: ok=") + (result.ok ? "1" : "0") +
                           " txn=" + result.transaction_id);
    } else {
        auto result = gate.revertBoost();
        gno_log::write(gno_log::Level::Info,
                       std::string("Boost revert via gate: ok=") + (result.ok ? "1" : "0") +
                           " " + result.detail);
    }
}

int main(int argc, char* argv[]) {
    // --smoke-test: прогон реального пути старта без человека за монитором.
    // Нужен для CI и scripts/smoke-test.sh: приложение поднимает всё как обычно,
    // но само выходит через пару секунд и не показывает модальных окон —
    // иначе проверка повисла бы навсегда на первом же QMessageBox.
    bool smoke_test = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--smoke-test")
            smoke_test = true;
    }

    // Лог инициализируем ДО установки обработчика Qt: иначе первые сообщения
    // Qt (в т.ч. о неподнявшемся platform-плагине) уйдут в никуда.
    gno_log::init("GNO");
    qInstallMessageHandler(myMessageHandler);

    gno_log::write(gno_log::Level::Info,
                   std::string("=== E2E4 Soft v") + gno::theme::APP_VERSION + " Starting ===");
    gno_log::write(gno_log::Level::Info, "App dir: " + QDir::currentPath().toStdString());

    QApplication app(argc, argv);
    app.setApplicationName("GNO");
    app.setApplicationVersion(gno::theme::APP_VERSION);
    app.setOrganizationName("GNO");

    gno_log::write(gno_log::Level::Info,
                   std::string("Platform: ") + QApplication::platformName().toStdString());

    QString appDir = QApplication::applicationDirPath();
    QStringList checks = {
        "platforms/qwindows.dll",
        "Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll"
    };
    bool allOk = true;
    for (const QString& f : checks) {
        bool ok = QFile::exists(appDir + "/" + f);
        gno_log::write(ok ? gno_log::Level::Info : gno_log::Level::Error,
                       "Check " + f.toStdString() + ": " + (ok ? "OK" : "MISSING!"));
        if (!ok) allOk = false;
    }

    if (!allOk) {
        if (!smoke_test) {
            QMessageBox::critical(nullptr, QString::fromUtf8("Ошибка E2E4 Soft"),
                QString::fromUtf8("Отсутствуют необходимые файлы!\nУбедитесь, что все DLL находятся в одной папке с E2E4.exe."));
        }
        gno_log::shutdown();
        return 1;
    }

    gno_log::write(gno_log::Level::Info, "Applying stylesheet...");
    QSettings settings;
    namespace sk = gno::SettingsKeys;
    bool darkTheme = settings.value(sk::Theme(), "dark").toString() != "light";
    app.setStyleSheet(gno::theme::globalStyleSheet(darkTheme));

    gno_log::write(gno_log::Level::Info, "Creating MainWindow...");
    try {
        gno::MainWindow window;
        gno::SystemTray tray;

        gno::MonitoringService& svc = gno::MonitoringService::instance();
        svc.setSoundEnabled(settings.value(sk::SoundNotifications(), true).toBool());
        svc.start();

        gno::OverlayWidget overlay;
        overlay.setCorner(static_cast<gno::OverlayWidget::Corner>(
            settings.value(sk::OverlayCorner(), 1).toInt()));
        overlay.setOpacityPercent(
            QList<int>{60, 75, 85, 95, 100}.value(settings.value(sk::OverlayOpacity(), 2).toInt(), 85));
        if (settings.value(sk::OverlayEnabled(), false).toBool())
            overlay.showOverlay();

        // settings page lives inside the main window; forward its signals
        QObject::connect(&window, &gno::MainWindow::themeChanged,
                &window, [&app, &settings](bool dark) {
            settings.setValue(gno::SettingsKeys::Theme(), dark ? "dark" : "light");
            app.setStyleSheet(gno::theme::globalStyleSheet(dark));
        });

        QObject::connect(&tray, &gno::SystemTray::showRequested,
                &window, &gno::MainWindow::forceShow);

        QObject::connect(&tray, &gno::SystemTray::boostToggled,
                &window, [&window](bool on) {
            applyBoost(on, window);
        });

        // Dashboard «ВКЛЮЧИТЬ ОПТИМИЗАЦИЮ» — тот же безопасный движок, что и в трее.
        QObject::connect(&window, &gno::MainWindow::boostRequested,
                &window, [&window](bool on) {
            applyBoost(on, window);
        });

        QObject::connect(&tray, &gno::SystemTray::overlayToggled,
                &overlay, &gno::OverlayWidget::toggleOverlay);

        QObject::connect(&tray, &gno::SystemTray::quitRequested,
                &app, &QApplication::quit);

        // service -> tray
        QObject::connect(&svc, &gno::MonitoringService::gameStarted, &window, [&tray](const QString& game) {
        const auto th = gno::AlertThresholds::forGame(game.toStdString());
        tray.setAlertThresholds(th.max_ping_ms, th.max_loss_percent);
    });
QObject::connect(&svc, &gno::MonitoringService::pingUpdated,
                &tray, &gno::SystemTray::updatePing);
        QObject::connect(&svc, &gno::MonitoringService::jitterUpdated,
                &tray, &gno::SystemTray::updateJitter);
        QObject::connect(&svc, &gno::MonitoringService::lossUpdated,
                &tray, &gno::SystemTray::updatePacketLoss);

        // service -> status bar
        QObject::connect(&svc, &gno::MonitoringService::gameStarted, &window, [&tray](const QString& game) {
        const auto th = gno::AlertThresholds::forGame(game.toStdString());
        tray.setAlertThresholds(th.max_ping_ms, th.max_loss_percent);
    });
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
        // Раньше remediationApplied эмитился и никем не принимался: результаты
        // автопилота, планового замера и автоскана молча пропадали, хотя
        // программа при этом меняла настройки системы.
        QObject::connect(&svc, &gno::MonitoringService::remediationApplied,
                &tray, [&tray](const QString& summary) {
            tray.showMessage(QString::fromUtf8("Оптимизация"), summary);
        });

        // settings page -> overlay / service / tray
        QObject::connect(&window, &gno::MainWindow::overlaySettingsChanged,
                &overlay, [&overlay](bool enabled, int corner, int opacity) {
            QSettings s;
            namespace sk = gno::SettingsKeys;
            s.setValue(sk::OverlayEnabled(), enabled);
            s.setValue(sk::OverlayCorner(), corner);
            s.setValue(sk::OverlayOpacity(), opacity);
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

        gno_log::write(gno_log::Level::Info,
                       "MainWindow + SystemTray + MonitoringService created, showing...");
        window.show();

        // Автопроверка обновлений: асинхронно, с задержкой — окно уже на
        // экране и остаётся отзывчивым, а диалог появляется только если
        // обновление действительно есть.
        if (!smoke_test && settings.value(sk::AutoUpdate(), true).toBool()) {
            gno_log::write(gno_log::Level::Info,
                           "Автопроверка обновлений запланирована через 2.5 с");
            QTimer::singleShot(2500, &window, [&window]() {
                // Живёт вместе с окном: запрос завершится уже после выхода
                // из этого лямбда-выражения.
                auto* checker = new gno::ui::UpdateChecker(&window);
                checker->checkAsync(&window);
            });
        } else if (!smoke_test) {
            gno_log::write(gno_log::Level::Info,
                           "Автопроверка обновлений отключена в настройках");
        }

        gno_log::write(gno_log::Level::Info, "=== Entering event loop ===");
        if (smoke_test) {
            // Даём событийному циклу провернуть стартовые задачи (таймеры
            // мониторинга, отрисовка первого кадра) и выходим сами.
            gno_log::write(gno_log::Level::Info, "smoke-test: auto-quit in 1500 ms");
            QTimer::singleShot(1500, &app, &QApplication::quit);
        }
        const int rc = app.exec();
        gno_log::write(gno_log::Level::Info, "=== Event loop finished, exiting ===");
        gno_log::shutdown();
        return rc;
    } catch (const std::exception& e) {
        gno_log::write(gno_log::Level::Critical, std::string("FATAL: ") + e.what());
        if (!smoke_test) {
            QMessageBox::critical(nullptr, QString::fromUtf8("Критическая ошибка E2E4 Soft"), QString("Exception: %1").arg(e.what()));
        }
        gno_log::shutdown();
        return 1;
    } catch (...) {
        gno_log::write(gno_log::Level::Critical, "FATAL: unknown exception");
        if (!smoke_test) {
            QMessageBox::critical(nullptr, QString::fromUtf8("Критическая ошибка E2E4 Soft"), QString::fromUtf8("Неизвестная ошибка"));
        }
        gno_log::shutdown();
        return 1;
    }
}