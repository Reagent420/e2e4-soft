#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <chrono>
#include <mutex>

namespace gno {

struct SessionRecord {
    std::string game_name;
    std::string start_time_str;
    std::string end_time_str;
    double avg_ping_ms = 0.0;
    double avg_jitter_ms = 0.0;
    double avg_packet_loss = 0.0;
    double max_ping_ms = 0.0;
    double quality_score = 0.0;
    uint32_t duration_seconds = 0;
    bool boost_was_active = false;
};

class SessionHistory {
public:
    // path — необязательный явный путь к файлу истории. Нужен тестам, чтобы не
    // писать в реальный %APPDATA%\GNO\history.json; по умолчанию — как раньше.
    explicit SessionHistory(std::string path = "");
    ~SessionHistory();

    void recordStart(const std::string& game_name, bool boost);
    void recordEnd(double avg_ping, double avg_jitter, double loss, double max_ping);
    void recordEndWithScore(double avg_ping, double avg_jitter, double loss, double max_ping, double quality_score);

    std::vector<SessionRecord> getAll() const;
    std::vector<SessionRecord> getLast(int count) const;
    void clear();

    bool saveToFile(const std::string& path = "") const;
    bool loadFromFile(const std::string& path = "");

    std::string getSavePath() const;
    bool exportCsv(const std::string& path) const;
    static constexpr std::size_t kMaxRecords = 500;
    double getAveragePing() const;
    double getAverageJitter() const;
    // Средние потери нужны для сводной оценки: без них оценка считалась с
    // потерями = 0 и расходилась с оценками в списке сессий.
    double getAverageLoss() const;

private:
    std::string getAppDataPath() const;

    // Версии без захвата mutex_. Вызывать их можно только из кода, который
    // mutex_ уже удерживает. Раньше recordEnd() и clear() вызывали публичные
    // saveToFile()/loadFromFile(), которые захватывают mutex_ повторно — а
    // std::mutex не рекурсивный. Первый же вызов recordEnd() (завершение игры,
    // плановый вечерний замер) навсегда вешал главный поток: приложение
    // замирало без окна ошибки, переставали идти таймеры и планировщик.
    bool saveToFileLocked(const std::string& path) const;
    bool loadFromFileLocked(const std::string& path);
    void recordEndLocked(double avg_ping, double avg_jitter, double loss, double max_ping,
                         double quality_score);

    mutable std::mutex mutex_;
    std::vector<SessionRecord> records_;
    SessionRecord current_;
    bool recording_ = false;
    std::string explicit_path_;
};

} // namespace gno
