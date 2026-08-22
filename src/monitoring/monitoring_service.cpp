#include "monitoring_service.h"
#include <QSettings>
#include <thread>
#include "../core/autopilot_plan.h"
#include "core/connection_grader.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <cmath>
#include <algorithm>

#include "game_profiles.h"
#include "network_utils.h"
#include "launch_diagnostics.h"
#include "../core/system_audit.h"

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#include <mmsystem.h>
#include <tlhelp32.h>
#pragma comment(lib, "winmm.lib")
#endif

namespace gno {

namespace {
constexpr int kLossWindow = 60;      // loss smoothed over 60 s
constexpr int kHistoryMax = 300;     // 5 minutes of 1 s samples
constexpr double kSpikeThreshold = 120.0;
constexpr double kSpikeRelease = 80.0;
}

MonitoringService& MonitoringService::instance() {
    static MonitoringService svc;
    return svc;
}

MonitoringService::MonitoringService() = default;

MonitoringService::~MonitoringService() {
    stop();
}

void MonitoringService::start() {
    if (timer_)
        return;

    ping_monitor_.setPingCallback([this](const ICMPResult& result) {
        std::lock_guard<std::mutex> lock(hist_mutex_);
        if (result.success) {
            last_ping_.store(result.latency_ms);
            ping_ok_.store(true);
            jitter_calc_.addSample(result.latency_ms);
            ping_hist_.push_back(result.latency_ms);
        } else {
            ping_ok_.store(false);
            ping_hist_.push_back(-1.0);
        }
        if (ping_hist_.size() > static_cast<size_t>(kHistoryMax))
            ping_hist_.erase(ping_hist_.begin());
    });
    ping_monitor_.start("1.1.1.1", 1000);

    GameWatcherConfig wcfg;
    wcfg.enabled = true;
    wcfg.check_interval_ms = 2000;
    wcfg.auto_apply_profiles = false;
    wcfg.notify_on_game_start = true;
    wcfg.notify_on_game_end = true;
    watcher_.setGameStartCallback([this](const std::string& game, const std::string& process, uint32_t) {
        QString name = QString::fromStdString(game);
        QString proc = QString::fromStdString(process);
        QMetaObject::invokeMethod(this, [this, name, proc]() { handleGameStart(name, proc); }, Qt::QueuedConnection);
    });
    watcher_.setGameEndCallback([this](const std::string& game, const std::string&) {
        QString name = QString::fromStdString(game);
        QMetaObject::invokeMethod(this, [this, name]() { handleGameEnd(name); }, Qt::QueuedConnection);
    });
    watcher_.start(wcfg);

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &MonitoringService::onTick);
    timer_->start(1000);
}

void MonitoringService::stop() {
    if (timer_) {
        timer_->stop();
        delete timer_;
        timer_ = nullptr;
    }
    watcher_.stop();
    ping_monitor_.stop();
    if (!current_game_.isEmpty())
        handleGameEnd(current_game_);
}

void MonitoringService::setBoostActive(bool active) {
    boost_active_ = active;
}

QVector<double> MonitoringService::pingHistory() const {
    std::lock_guard<std::mutex> lock(hist_mutex_);
    QVector<double> out;
    out.reserve(static_cast<int>(ping_hist_.size()));
    for (double v : ping_hist_) out.append(v);
    return out;
}

QVector<double> MonitoringService::jitterHistory() const {
    std::lock_guard<std::mutex> lock(hist_mutex_);
    QVector<double> out;
    out.reserve(static_cast<int>(jitter_hist_.size()));
    for (double v : jitter_hist_) out.append(v);
    return out;
}

QVector<double> MonitoringService::lossHistory() const {
    std::lock_guard<std::mutex> lock(hist_mutex_);
    QVector<double> out;
    out.reserve(static_cast<int>(loss_hist_.size()));
    for (double v : loss_hist_) out.append(v);
    return out;
}

void MonitoringService::startMeasure(const QString& label) {
    measuring_ = true;
    measure_label_ = label;
    measure_pings_.clear();
}

void MonitoringService::stopMeasure() {
    measuring_ = false;
    measure_label_.clear();
}

double MonitoringService::measurePingAvg() const {
    if (measure_pings_.empty()) return 0.0;
    double sum = 0.0;
    for (double v : measure_pings_) sum += v;
    return sum / static_cast<double>(measure_pings_.size());
}

QStringList MonitoringService::getRecommendations() const {
    QStringList recs;
    auto records = history_.getLast(50);
    if (records.empty()) {
        recs << QString::fromUtf8("Играйте в игры — после первых сессий появятся персональные рекомендации.");
        return recs;
    }

    double totalPing = 0.0, totalJitter = 0.0, totalLoss = 0.0;
    double worstLoss = 0.0, worstJitter = 0.0, worstPing = 0.0;
    std::string worstLossGame, worstJitterGame, worstPingGame;
    int eveningLoss = 0, eveningSessions = 0;

    for (const auto& r : records) {
        totalPing += r.avg_ping_ms;
        totalJitter += r.avg_jitter_ms;
        totalLoss += r.avg_packet_loss;

        if (r.avg_packet_loss > worstLoss) { worstLoss = r.avg_packet_loss; worstLossGame = r.game_name; }
        if (r.avg_jitter_ms > worstJitter) { worstJitter = r.avg_jitter_ms; worstJitterGame = r.game_name; }
        if (r.avg_ping_ms > worstPing) { worstPing = r.avg_ping_ms; worstPingGame = r.game_name; }

        int hour = -1;
        if (r.start_time_str.size() >= 13) {
            hour = std::atoi(r.start_time_str.substr(11, 2).c_str());
        }
        if (hour >= 18 && hour <= 23) {
            ++eveningSessions;
            if (r.avg_packet_loss > 1.0) ++eveningLoss;
        }
    }

    double n = static_cast<double>(records.size());
    double avgPing = totalPing / n;
    double avgJitter = totalJitter / n;
    double avgLoss = totalLoss / n;

    if (avgLoss > 2.0)
        recs << QString::fromUtf8("Средние потери пакетов %1% — включите компенсацию потерь и мультимаршрутный режим.")
                    .arg(avgLoss, 0, 'f', 1);
    else if (worstLoss > 3.0)
        recs << QString::fromUtf8("В игре «%1» потери достигали %2% — проверьте Wi-Fi или используйте проводное соединение.")
                    .arg(QString::fromStdString(worstLossGame))
                    .arg(worstLoss, 0, 'f', 1);

    if (avgJitter > 6.0)
        recs << QString::fromUtf8("Джиттер %1 мс — соединение нестабильно. Смените DNS или включите автовыбор маршрута.")
                    .arg(avgJitter, 0, 'f', 1);
    else if (worstJitter > 10.0)
        recs << QString::fromUtf8("В игре «%1» джиттер достигал %2 мс — рекомендуем включить стабилизацию маршрута.")
                    .arg(QString::fromStdString(worstJitterGame))
                    .arg(worstJitter, 0, 'f', 1);

    if (avgPing > 90.0)
        recs << QString::fromUtf8("Средний пинг %1 мс — выполните бенчмарк DNS на вкладке «Сеть» и примените самый быстрый сервер.")
                    .arg(avgPing, 0, 'f', 1);

    if (eveningSessions >= 3 && eveningLoss > 0 && eveningLoss * 2 >= eveningSessions)
        recs << QString::fromUtf8("Вечером соединение заметно хуже — включайте оптимизацию перед вечерними матчами.");

    if (recs.isEmpty())
        recs << QString::fromUtf8("Соединение стабильно! Держите оптимизацию включённой и играйте без лагов.");

    return recs;
}

void MonitoringService::onTick() {
    bool ok = ping_ok_.load();

    // jitter from calculator
    double jitter = 0.0;
    {
        auto stats = jitter_calc_.getStats();
        jitter = stats.current_jitter_ms;
    }

    // loss: smoothed 1/0 per second over a 60 s window
    {
        std::lock_guard<std::mutex> lock(hist_mutex_);
        loss_hist_.push_back(ok ? 0.0 : 1.0);
        if (loss_hist_.size() > static_cast<size_t>(kLossWindow))
            loss_hist_.erase(loss_hist_.begin());
    }
    double lossSum = 0.0;
    for (double v : loss_hist_) lossSum += v;
    double lossPercent = lossSum / static_cast<double>(loss_hist_.size()) * 100.0;

    std::lock_guard<std::mutex> lock(hist_mutex_);
    jitter_hist_.push_back(jitter);
    if (jitter_hist_.size() > static_cast<size_t>(kHistoryMax))
        jitter_hist_.erase(jitter_hist_.begin());

    last_jitter_.store(jitter);
    last_loss_.store(lossPercent);

    double ping = ok ? last_ping_.load() : -1.0;
    if (ping > session_max_ping_) session_max_ping_ = ping;
    if (ping > max_ping_.load()) max_ping_.store(ping);

    emit pingUpdated(ping);
    emit jitterUpdated(jitter);
    emit lossUpdated(lossPercent);

    // session accumulation
    if (!current_game_.isEmpty() && ping >= 0.0) {
        session_ping_sum_ += ping;
        session_jitter_sum_ += jitter;
        session_loss_sum_ += lossPercent;
        session_max_ping_ = std::max(session_max_ping_, ping);
        ++session_samples_;
    }

    // comparison measurement
    if (measuring_ && ping >= 0.0)
        measure_pings_.push_back(ping);

    // spike alert (only while a game is running)
    if (!current_game_.isEmpty()) {
        if (ok && ping > kSpikeThreshold && !spike_active_) {
            spike_active_ = true;
            playEventSound(QStringLiteral("alert"));
        } else if (ok && ping < kSpikeRelease && spike_active_) {
            spike_active_ = false;
        }
    }
}

void MonitoringService::handleGameStart(const QString& game, const QString& process) {
    current_game_ = game;
    session_clock_.restart();
    session_ping_sum_ = 0.0;
    session_jitter_sum_ = 0.0;
    session_loss_sum_ = 0.0;
    session_max_ping_ = 0.0;
    session_samples_ = 0;
    spike_active_ = false;

    history_.recordStart(game.toStdString(), boost_active_);
    collector_.start(game.toStdString());
    playEventSound(QStringLiteral("game_start"));

    // auto-apply the saved per-game profile (non-VPN actions only)
    try {
        GameProfiles profiles;
        if (profiles.has(game.toStdString())) {
            GameProfile p = profiles.get(game.toStdString());
            if (p.auto_apply &&
                QSettings().value(QStringLiteral("remediation/autopilot"), true).toBool()) {
                // v1.6.1: apply on a worker thread - never freeze the UI at game launch.
                std::thread([this, game, process, p]() {
                    QStringList notes;
                    for (const auto& id : AutopilotPlan::actionIdsFor(p)) {
                        LaunchDiagnostics::applyFix(id);
                        notes << QString::fromUtf8(AutopilotPlan::displayName(id));
                    }
                    QMetaObject::invokeMethod(this, [this, game, notes]() {
                        emit gameStarted(game + (notes.isEmpty() ? QString()
                            : QString::fromUtf8("\x20\x2D\x20\xD0\xBF\xD1\x80\xD0\xBE\xD1\x84\xD0\xB8\xD0\xBB\xD1\x8C\x20\xD0\xBF\xD1\x80\xD0\xB8\xD0\xBC\xD0\xB5\xD0\xBD\xD1\x91\xD0\xBD\x3A\x20") + notes.join(", ")));
                        emit remediationApplied(QString::fromUtf8("\xD0\x90\xD0\xB2\xD1\x82\xD0\xBE\xD0\xBF\xD0\xB8\xD0\xBB\xD0\xBE\xD1\x82\x3A\x20") + game +
                            QString::fromUtf8("\x3A\x20") + notes.join(", ") +
                            QString::fromUtf8("\x20\x28\xD1\x81\x20\xD0\xB1\xD1\x8D\xD0\xBA\xD0\xB0\xD0\xBF\xD0\xBE\xD0\xBC\x29"));
                    }, Qt::QueuedConnection);
                }).detach();
} else {
                emit gameStarted(game);
            }
        } else {
            emit gameStarted(game);
        }
    } catch (...) {
        emit gameStarted(game);
    }

    emit diagnosticsTriggered(game, process);
}

void MonitoringService::handleGameEnd(const QString& game) {
    double avgPing = 0.0, avgJitter = 0.0, avgLoss = 0.0;
    if (session_samples_ > 0) {
        avgPing = session_ping_sum_ / session_samples_;
        avgJitter = session_jitter_sum_ / session_samples_;
        avgLoss = session_loss_sum_ / session_samples_;
    }
    const auto grade = ConnectionGrader::evaluate(avgPing, avgJitter, avgLoss);
    history_.recordEndWithScore(avgPing, avgJitter, avgLoss, session_max_ping_, static_cast<double>(grade.score));
    collector_.stop();
    current_game_.clear();

    playEventSound(QStringLiteral("game_end"));
    emit gameEnded(game);
    emit sessionRecorded();

    auto recs = getRecommendations();
    if (!recs.isEmpty())
        emit recommendationAvailable(recs.first());
}

void MonitoringService::playEventSound(const QString& kind) {
    if (!sound_enabled_) return;
    if (kind == QStringLiteral("game_start")) {
        playTone(660, 90);
        playTone(880, 110);
    } else if (kind == QStringLiteral("game_end")) {
        playTone(880, 90);
        playTone(660, 110);
    } else if (kind == QStringLiteral("alert")) {
        playTone(990, 220);
    }
}

void MonitoringService::playTone(int freqHz, int durationMs) {
#ifdef PLATFORM_WINDOWS
    constexpr int kRate = 44100;
    const int totalSamples = kRate * durationMs / 1000;
    const int bytes = 44 + totalSamples * 2;

    // keep the buffer alive while PlaySound plays asynchronously
    static std::vector<BYTE> buffer;
    buffer.assign(bytes, 0);

    auto put32 = [&](int off, int v) {
        buffer[off] = static_cast<BYTE>(v & 0xFF);
        buffer[off + 1] = static_cast<BYTE>((v >> 8) & 0xFF);
        buffer[off + 2] = static_cast<BYTE>((v >> 16) & 0xFF);
        buffer[off + 3] = static_cast<BYTE>((v >> 24) & 0xFF);
    };
    auto put16 = [&](int off, int v) {
        buffer[off] = static_cast<BYTE>(v & 0xFF);
        buffer[off + 1] = static_cast<BYTE>((v >> 8) & 0xFF);
    };

    memcpy(&buffer[0], "RIFF", 4);
    put32(4, 36 + totalSamples * 2);
    memcpy(&buffer[8], "WAVE", 4);
    memcpy(&buffer[12], "fmt ", 4);
    put32(16, 16);
    put16(20, 1);          // PCM
    put16(22, 1);          // mono
    put32(24, kRate);
    put32(28, kRate * 2);  // byte rate
    put16(32, 2);          // block align
    put16(34, 16);         // bits per sample
    memcpy(&buffer[36], "data", 4);
    put32(40, totalSamples * 2);

    const double attack = 0.02; // 20 ms fade in/out to avoid clicks
    for (int i = 0; i < totalSamples; ++i) {
        double t = static_cast<double>(i) / kRate;
        double env = 1.0;
        if (t < attack) env = t / attack;
        if (t > static_cast<double>(durationMs) / 1000.0 - attack)
            env = std::max(0.0, (static_cast<double>(durationMs) / 1000.0 - t) / attack);
        short sample = static_cast<short>(env * 0.35 * 32767.0 * std::sin(2.0 * M_PI * freqHz * t));
        put16(44 + i * 2, sample);
    }

    PlaySoundW(reinterpret_cast<LPCWSTR>(buffer.data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
#endif
}

} // namespace gno