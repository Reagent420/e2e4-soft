#include "logger.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>

namespace gno {
namespace log {
namespace {

std::mutex g_mutex;
std::ofstream g_stream;
std::string g_path;

// std::localtime возвращает указатель на общий статический буфер — под своим
// мьютексом, чтобы два потока не перетёрли друг другу дату.
std::tm localTime(std::time_t t) {
    static std::mutex time_mutex;
    std::lock_guard<std::mutex> lk(time_mutex);
    std::tm out{};
#if defined(_WIN32)
    localtime_s(&out, &t);
#else
    localtime_r(&t, &out);
#endif
    return out;
}

std::string dateString(std::time_t t) {
    const std::tm tm = localTime(t);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return buf;
}

std::string timestamp() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    const std::tm tm = localTime(system_clock::to_time_t(now));
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d",
                  tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms.count()));
    return buf;
}

const char* levelName(Level level) {
    switch (level) {
        case Level::Debug:    return "DEBUG";
        case Level::Info:     return "INFO";
        case Level::Warning:  return "WARN";
        case Level::Error:    return "ERROR";
        case Level::Critical: return "CRIT";
    }
    return "?";
}

// Каталог логов: <данные пользователя>/<app_name>/logs.
std::string logDir(const std::string& app_name, const std::string& override_base) {
    std::filesystem::path base;
    if (!override_base.empty()) {
        base = override_base;
    } else {
#if defined(_WIN32)
        const char* appdata = std::getenv("APPDATA");
        if (appdata && *appdata) base = appdata;
#else
        const char* xdg = std::getenv("XDG_DATA_HOME");
        if (xdg && *xdg) {
            base = xdg;
        } else {
            const char* home = std::getenv("HOME");
            if (home && *home) base = std::filesystem::path(home) / ".local" / "share";
        }
#endif
        // Переменных окружения может не быть вовсе: урезанное окружение,
        // запуск из службы, песочница. Тогда пишем во временный каталог — он
        // гарантированно доступен на запись. Раньше фолбэком был текущий
        // рабочий каталог, и приложение оставляло папку GNO там, откуда его
        // запустили (в том числе в Program Files, где писать нельзя).
        if (base.empty()) {
            std::error_code ec;
            base = std::filesystem::temp_directory_path(ec);
            if (ec) base = std::filesystem::path(".");
        }
    }
    return (base / app_name / "logs").string();
}

} // namespace

void rotate(const std::string& log_dir, int keep_files) {
    if (keep_files < 1) keep_files = 1;

    std::error_code ec;
    std::filesystem::directory_iterator it(log_dir, ec);
    if (ec) return;

    std::vector<std::filesystem::path> logs;
    for (const auto& entry : it) {
        if (!entry.is_regular_file(ec)) continue;
        const std::string name = entry.path().filename().string();
        // Только свои файлы: gno-YYYY-MM-DD.log — ровно 18 символов
        // (4 префикс + 10 дата + 4 расширение). Чужие (например,
        // gno-crash.dmp) ротация не трогает.
        if (name.size() == 18 && name.rfind("gno-", 0) == 0 &&
            name.substr(name.size() - 4) == ".log") {
            logs.push_back(entry.path());
        }
    }

    if (static_cast<int>(logs.size()) <= keep_files) return;

    // Имена — ISO-даты, поэтому лексикографическая сортировка = хронологическая.
    std::sort(logs.begin(), logs.end());
    const std::size_t remove_count = logs.size() - static_cast<std::size_t>(keep_files);
    for (std::size_t i = 0; i < remove_count; ++i) {
        std::filesystem::remove(logs[i], ec);
    }
}

void init(const std::string& app_name, const std::string& base_dir_override) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_stream.is_open()) g_stream.close();
    g_path.clear();

    const std::string dir = logDir(app_name, base_dir_override);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return;   // нет каталога — логирование молча отключается

    rotate(dir, kDefaultKeepFiles);

    g_path = dir + "/gno-" + dateString(std::time(nullptr)) + ".log";
    g_stream.open(g_path, std::ios::app);
}

void write(Level level, std::string_view message) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_stream.is_open()) return;
    g_stream << '[' << timestamp() << "] [" << levelName(level) << "] "
             << message << '\n';
    // Сбрасываем сразу: если приложение упадёт на следующей строке, последняя
    // запись всё равно останется в файле — ради этого лог и заводился.
    g_stream.flush();
}

std::string currentLogPath() {
    std::lock_guard<std::mutex> lk(g_mutex);
    return g_path;
}

void shutdown() {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_stream.is_open()) g_stream.close();
    g_path.clear();
}

} // namespace log
} // namespace gno
