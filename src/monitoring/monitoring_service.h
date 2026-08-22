#pragma once

#include <QObject>
#include <QTimer>
#include <QVector>
#include <QString>
#include <QElapsedTimer>
#include <QStringList>
#include <atomic>
#include <mutex>
#include <vector>

#include "ping_monitor.h"
#include "jitter_calculator.h"
#include "stats_collector.h"
#include "../core/session_history.h"
#include "../core/game_watcher.h"

namespace gno {

// Central monitoring service: one shared ping monitor, session recording,
// game watching, recommendations, comparison measurements and event sounds.
// Owns all live network data so every widget (dashboard, monitoring, tray,
// overlay) shows the same numbers.
class MonitoringService : public QObject {
    Q_OBJECT

private slots:
    void checkSchedule();
public:
    static MonitoringService& instance();

    void start();
    void stop();

    double currentPing() const { return last_ping_.load(); }
    double currentJitter() const { return last_jitter_.load(); }
    double currentLossPercent() const { return last_loss_.load(); }
    double currentMaxPing() const { return max_ping_.load(); }
    bool hasPing() const { return ping_ok_.load(); }
    QString currentGame() const { return current_game_; }
    bool isBoostActive() const { return boost_active_; }
    void setBoostActive(bool active);
    bool soundEnabled() const { return sound_enabled_; }
    void setSoundEnabled(bool on) { sound_enabled_ = on; }

    SessionHistory* history() { return &history_; }
    StatsCollector* collector() { return &collector_; }

    QVector<double> pingHistory() const;
    QVector<double> jitterHistory() const;
    QVector<double> lossHistory() const;

    // Comparison measurement (before/after boost)
    void startMeasure(const QString& label);
    void stopMeasure();
    bool isMeasuring() const { return measuring_; }
    QString measureLabel() const { return measure_label_; }
    double measurePingAvg() const;
    uint32_t measureSamples() const { return static_cast<uint32_t>(measure_pings_.size()); }

    QStringList getRecommendations() const;

signals:
    void remediationApplied(const QString& summary);
    void pingUpdated(double ms);
    void jitterUpdated(double ms);
    void lossUpdated(double percent);
    void gameStarted(const QString& gameName);
    void gameEnded(const QString& gameName);
    void sessionRecorded();
    void recommendationAvailable(const QString& text);
    void diagnosticsTriggered(const QString& gameName, const QString& processName);

private slots:
    void onTick();

private:
    MonitoringService();
    ~MonitoringService() override;

    void handleGameStart(const QString& game, const QString& process = QString());
    void handleGameEnd(const QString& game);
    void playEventSound(const QString& kind);
    void playTone(int freqHz, int durationMs);

    PingMonitor ping_monitor_;
    JitterCalculator jitter_calc_;
    StatsCollector collector_;
    SessionHistory history_;
    GameWatcher watcher_;
    QTimer* timer_ = nullptr;
    QTimer* scheduler_timer_ = nullptr;
    QString last_schedule_date_;
    std::atomic<double> last_ping_{0.0};
    std::atomic<double> last_jitter_{0.0};
    std::atomic<double> last_loss_{0.0};
    std::atomic<double> max_ping_{0.0};
    std::atomic<bool> ping_ok_{false};

    mutable std::mutex hist_mutex_;
    std::vector<double> ping_hist_;
    std::vector<double> jitter_hist_;
    std::vector<double> loss_hist_;

    QString current_game_;
    QElapsedTimer session_clock_;
    bool boost_active_ = false;
    bool sound_enabled_ = true;

    double session_ping_sum_ = 0.0;
    double session_jitter_sum_ = 0.0;
    double session_loss_sum_ = 0.0;
    double session_max_ping_ = 0.0;
    uint32_t session_samples_ = 0;

    bool measuring_ = false;
    QString measure_label_;
    std::vector<double> measure_pings_;

    bool spike_active_ = false;
};

} // namespace gno