#include "update_checker.h"

#include "core/logger.h"
#include "core/sha256.h"
#include "core/updater.h"
#include "theme.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

namespace gno {
namespace ui {
namespace {

constexpr int kNetworkTimeoutMs = 15000;

// GitHub API требует User-Agent, иначе отвечает 403.
QNetworkRequest makeRequest(const QString& url) {
    // Фигурные скобки, а не круглые: QNetworkRequest request(QUrl(url))
    // компилятор читает как объявление функции (most vexing parse).
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QString("GNO-Updater/%1").arg(theme::APP_VERSION));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    return request;
}

// Синхронный GET. Используется только на пути, запущенном кнопкой, где
// короткая заморозка интерфейса ожидаема.
bool httpGetSync(const std::string& url, std::string& body, std::string& error) {
    QNetworkAccessManager manager;
    QNetworkReply* reply = manager.get(makeRequest(QString::fromStdString(url)));

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(kNetworkTimeoutMs);
    loop.exec();

    if (reply->isRunning()) {
        reply->abort();
        error = "истёк таймаут запроса";
        reply->deleteLater();
        return false;
    }

    if (reply->error() != QNetworkReply::NoError) {
        error = reply->errorString().toStdString();
        reply->deleteLater();
        return false;
    }

    body = reply->readAll().toStdString();
    reply->deleteLater();
    error.clear();
    return true;
}

// Скачивание в файл. Пишем потоком: обновление может быть большим, держать
// его целиком в памяти незачем.
bool downloadSync(const std::string& url, const std::string& dest_path,
                  std::string& error) {
    QNetworkAccessManager manager;
    QNetworkReply* reply = manager.get(makeRequest(QString::fromStdString(url)));

    const QString dest = QString::fromStdString(dest_path);
    QDir().mkpath(QFileInfo(dest).absolutePath());
    QFile out(dest);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        error = "не удалось открыть файл для записи: " + dest.toStdString();
        reply->deleteLater();
        return false;
    }

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    // Данные пишем по мере поступления — тогда даже при таймауте файл
    // останется частично записанным и его удалит вызывающий код.
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, [&]() {
        out.write(reply->readAll());
    });
    timer.start(kNetworkTimeoutMs);
    loop.exec();

    if (reply->isRunning()) {
        reply->abort();
        out.close();
        error = "истёк таймаут скачивания";
        reply->deleteLater();
        return false;
    }

    out.write(reply->readAll());
    out.close();

    if (reply->error() != QNetworkReply::NoError) {
        error = reply->errorString().toStdString();
        reply->deleteLater();
        return false;
    }

    reply->deleteLater();
    error.clear();
    return true;
}

bool hashFileSync(const std::string& path, std::string& hex, std::string& error) {
    return gno::sha256::hashFile(path, hex, error);
}

QString downloadDir() {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (dir.isEmpty()) dir = QDir::tempPath();
    return dir + "/GNO-update";
}

} // namespace

UpdateChecker::UpdateChecker(QObject* parent) : QObject(parent) {}

QString UpdateChecker::fetchVerified(const QString& url, const QString& expected_sha256,
                                     const QString& file_name, QString& error) {
    updater::Asset asset;
    asset.name = file_name.toStdString();
    asset.download_url = url.toStdString();
    asset.sha256 = expected_sha256.toStdString();

    updater::Updater engine;
    engine.setHttpDownload(downloadSync);
    engine.setFileHasher(hashFileSync);

    const QString dest = downloadDir() + "/" + file_name;
    std::string actual;
    std::string err;
    if (!engine.downloadAndVerify(asset, dest.toStdString(), actual, err)) {
        error = QString::fromStdString(err);
        return QString();
    }
    gno::log::write(gno::log::Level::Info,
                    "Обновление скачано и проверено по SHA-256: " + actual);
    return dest;
}

void UpdateChecker::checkInteractive(QWidget* parent) {
    updater::Updater engine;
    engine.setHttpGet(httpGetSync);
    engine.setHttpDownload(downloadSync);
    engine.setFileHasher(hashFileSync);

    presentResult(parent, engine.check(theme::APP_VERSION), /*silent=*/false);
}

void UpdateChecker::checkAsync(QWidget* parent) {
    // Штатный асинхронный запрос Qt: никаких потоков и вложенных циклов
    // событий, окно остаётся живым. Объект живёт до ответа — родитель-окно
    // гарантирует это, а QPointer страхует от разрушения раньше времени.
    auto* manager = new QNetworkAccessManager(this);
    QPointer<QWidget> parent_guard(parent);

    QNetworkReply* reply =
        manager->get(makeRequest(QString::fromStdString(updater::Updater::releasesApiUrl())));
    gno::log::write(gno::log::Level::Debug, "Обновления: запрос отправлен");

    auto* timer = new QTimer(this);
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, reply, &QNetworkReply::abort);

    // Подписку оформляем до запуска таймера: если ответ придёт мгновенно
    // (например, ошибка TLS), сигнал не должен обогнать обработчик.
    QObject::connect(reply, &QNetworkReply::finished, this,
                     [this, reply, manager, timer, parent_guard]() {
        timer->stop();
        const bool ok = (reply->error() == QNetworkReply::NoError);
        const QByteArray body = reply->readAll();
        reply->deleteLater();
        manager->deleteLater();
        timer->deleteLater();

        if (!ok) {
            // При старте о сетевой неудаче молчим: пользователь не просил.
            gno::log::write(gno::log::Level::Debug,
                            "Автопроверка обновлений не удалась (сеть недоступна)");
            return;
        }

        const updater::CheckResult result =
            updater::parseLatestRelease(body.toStdString(), theme::APP_VERSION);

        // Диалогов при старте не показываем, но исход фиксируем в логе —
        // иначе «тихая» проверка становится неотличима от «не выполнялась».
        if (!result.ok) {
            gno::log::write(gno::log::Level::Warning,
                            "Автопроверка обновлений: " + result.error);
            return;
        }
        if (!result.update_available) {
            gno::log::write(gno::log::Level::Info,
                            std::string("Обновлений нет, текущая версия ") +
                                theme::APP_VERSION);
            return;
        }
        presentResult(parent_guard.data(), result, /*silent=*/true);
    });

    timer->start(kNetworkTimeoutMs);
}

void UpdateChecker::presentResult(QWidget* parent, const updater::CheckResult& result,
                                  bool silent) {
    if (!result.ok) {
        gno::log::write(gno::log::Level::Warning,
                        "Проверка обновлений: " + result.error);
        if (!silent) {
            QMessageBox::warning(parent, QObject::tr("Проверка обновлений"),
                                 QObject::tr("Не удалось проверить обновления.\n\n%1")
                                     .arg(QString::fromStdString(result.error)));
        }
        return;
    }

    if (!result.update_available) {
        gno::log::write(gno::log::Level::Info,
                        std::string("Обновлений нет, текущая версия ") + theme::APP_VERSION);
        if (!silent) {
            QMessageBox::information(
                parent, QObject::tr("Проверка обновлений"),
                QObject::tr("У вас последняя версия: %1").arg(theme::APP_VERSION));
        }
        return;
    }

    const QString latest = QString::fromStdString(result.latest_version);
    gno::log::write(gno::log::Level::Info,
                    "Доступно обновление: " + latest.toStdString());

    QString notes = QString::fromStdString(result.release.notes).trimmed();
    if (notes.size() > 1200) notes = notes.left(1200) + "…";

    QMessageBox box(parent);
    box.setWindowTitle(QObject::tr("Доступно обновление"));
    box.setIcon(QMessageBox::Information);
    box.setText(QObject::tr("Доступна версия %1 (у вас %2).")
                    .arg(latest, QString::fromUtf8(theme::APP_VERSION)));
    if (!notes.isEmpty()) box.setInformativeText(notes);

    const updater::Asset* asset = updater::pickWindowsAsset(result.release);
    QPushButton* download_button = nullptr;
    if (asset != nullptr) {
        download_button = box.addButton(QObject::tr("Скачать и проверить"),
                                        QMessageBox::AcceptRole);
    }
    QPushButton* page_button = box.addButton(QObject::tr("Открыть страницу релиза"),
                                             QMessageBox::ActionRole);
    box.addButton(QObject::tr("Позже"), QMessageBox::RejectRole);
    box.exec();

    if (download_button != nullptr && box.clickedButton() == download_button) {
        QString sha = QString::fromStdString(asset->sha256);
        // Дайджеста в релизе может не быть — тогда пробуем sidecar-файл
        // <имя>.sha256. Без хеша скачивание не начинаем вообще.
        if (sha.isEmpty()) {
            const updater::Asset* sidecar =
                updater::findSidecarAsset(result.release, *asset);
            if (sidecar != nullptr) {
                std::string body;
                std::string err;
                if (httpGetSync(sidecar->download_url, body, err)) {
                    std::string hex;
                    if (updater::parseSidecarDigest(body, hex))
                        sha = QString::fromStdString(hex);
                }
            }
        }

        if (sha.isEmpty()) {
            QMessageBox::warning(
                parent, QObject::tr("Обновление"),
                QObject::tr("В релизе нет контрольной суммы SHA-256 для файла %1.\n\n"
                            "Установка непроверенного файла запрещена — скачайте "
                            "обновление вручную со страницы релиза.")
                    .arg(QString::fromStdString(asset->name)));
            return;
        }

        QString error;
        const QString path = fetchVerified(QString::fromStdString(asset->download_url),
                                           sha, QString::fromStdString(asset->name), error);
        if (path.isEmpty()) {
            QMessageBox::warning(parent, QObject::tr("Обновление"),
                                 QObject::tr("Не удалось скачать обновление.\n\n%1").arg(error));
            return;
        }

        QMessageBox done(parent);
        done.setWindowTitle(QObject::tr("Обновление скачано"));
        done.setIcon(QMessageBox::Information);
        done.setText(QObject::tr("Файл проверен по SHA-256 и сохранён:\n%1").arg(path));
        // Релизы выкладывают архив — без этой подсказки пользователь
        // попытается «запустить» zip.
        const bool is_archive = path.endsWith(".zip", Qt::CaseInsensitive);
        done.setInformativeText(
            is_archive ? QObject::tr("Закройте программу, распакуйте архив и замените "
                                     "файлы установки на новые.")
                       : QObject::tr("Закройте программу и запустите файл, "
                                     "чтобы установить обновление."));
        QPushButton* open_folder = done.addButton(QObject::tr("Открыть папку"),
                                                  QMessageBox::ActionRole);
        done.addButton(QObject::tr("Закрыть"), QMessageBox::RejectRole);
        done.exec();
        if (done.clickedButton() == open_folder)
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
        return;
    }

    if (box.clickedButton() == page_button && !result.release.html_url.empty()) {
        QDesktopServices::openUrl(
            QUrl(QString::fromStdString(result.release.html_url)));
    }
}

} // namespace ui
} // namespace gno
