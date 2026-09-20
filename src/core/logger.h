#pragma once

// Единый лог сессии. Раньше всё уходило в fprintf(stderr, ...) — при запуске
// из ярлыка (WIN32-подсистема, без консоли) stderr уходил в никуда, и по
// жалобе «программа не запустилась» нечего было смотреть. Теперь каждая
// сессия пишет файл в каталог данных пользователя:
//   Windows: %APPDATA%\GNO\logs\gno-YYYY-MM-DD.log
//   Linux:   $XDG_DATA_HOME/GNO/logs или ~/.local/share/GNO/logs
//
// Модуль намеренно Qt-free: GNO-tests не линкует Qt, а логирование хочется
// тестировать (создание файла, ротация, конкурентная запись).

#include <string>
#include <string_view>

namespace gno {
namespace log {

enum class Level { Debug, Info, Warning, Error, Critical };

// Сколько файлов gno-*.log оставлять при ротации.
constexpr int kDefaultKeepFiles = 7;

// Открывает файл текущей сессии. app_name — подкаталог в данных пользователя
// («GNO»). base_dir_override — для тестов: если не пусто, каталог логов =
// <override>/logs, а %APPDATA% не трогается.
// Повторный вызов переоткрывает файл (полезно после смены даты).
void init(const std::string& app_name, const std::string& base_dir_override = "");

// Запись одной строки. Потокобезопасна. Если init() не вызывался — no-op:
// лучше потерять строку, чем уронить приложение из-за лога.
void write(Level level, std::string_view message);

// Путь к текущему файлу лога; пустая строка, если init() не вызывался.
std::string currentLogPath();

// Оставляет не более keep_files самых свежих gno-*.log в каталоге, остальные
// удаляет. Вынесена отдельно от init(), чтобы её можно было проверить тестом
// без создания настоящей сессии.
void rotate(const std::string& log_dir, int keep_files = kDefaultKeepFiles);

// Закрывает файл и сбрасывает состояние. Нужно тестам и корректному выходу.
void shutdown();

} // namespace log
} // namespace gno
