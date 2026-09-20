#pragma once

#include <QObject>
#include <QString>

class QWidget;

namespace gno {
namespace updater {
struct CheckResult;
}
namespace ui {

// UI-обвязка проверки обновлений: сеть отдаёт Qt, вся логика — в core/updater.
// Разделение сделано ради тестируемости: core/updater собирается и проверяется
// без Qt, а здесь остаются только запросы и диалоги.
class UpdateChecker : public QObject {
    Q_OBJECT
public:
    explicit UpdateChecker(QObject* parent = nullptr);

    // Проверка по нажатию кнопки: синхронная, с показом результата в обоих
    // случаях («есть обновление» / «у вас последняя версия» / «не удалось»).
    // Интерфейс на время запроса замирает — это осознанный компромисс ради
    // линейного кода; пользователь сам инициировал действие.
    void checkInteractive(QWidget* parent);

    // Проверка при старте: асинхронная и молчаливая. Окно не блокируется,
    // диалог появляется только если обновление действительно есть.
    // Объект должен жить дольше запроса — создавайте его с родителем-окном.
    void checkAsync(QWidget* parent);

private:
    // Общий хвост: разбор результата и диалоги. silent подавляет сообщения
    // «обновлений нет» и «не удалось проверить».
    void presentResult(QWidget* parent, const updater::CheckResult& result, bool silent);

    // Скачивает и проверяет SHA-256. Возвращает путь к файлу либо пустую строку.
    QString fetchVerified(const QString& url, const QString& expected_sha256,
                          const QString& file_name, QString& error);
};

} // namespace ui
} // namespace gno
