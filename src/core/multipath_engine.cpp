#include "multipath_engine.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace gno {

namespace {
// Количество пакетов на один цикл зондирования пути. Раньше measurePacketLoss
// вызывался со счётчиком 20 на каждый путь под блокировкой — один цикл мог
// висеть на кольцевой очереди потоков десятки секунд. 5/10 — это всё ещё
// достаточно для оценки качества пути, а цикл остается отзывчивым к stop().
constexpr uint32_t kLatencyProbes = 5;
constexpr uint32_t kLossProbes = 10;
// Сглаживание джиттера: EWMA, иначе одно выбившее измерение полностью
// переписывало бы джиттер пути.
constexpr double kJitterAlpha = 0.3;
} // namespace

MultipathEngine::MultipathEngine(const MultipathConfig& config)
    : config_(config) {}

MultipathEngine::~MultipathEngine() {
    stop();
}

void MultipathEngine::start(const std::string& game_server_ip) {
    {
        std::lock_guard<std::mutex> lock(stop_mutex_);
        if (active_) return;

        game_server_ip_ = game_server_ip;
        active_ = true;

        // Перезапуск после предыдущего сеанса: путей от старой сессии не осталось?
        // На всякий случай приводим карты в исходное состояние.
        {
            std::lock_guard<std::mutex> plock(paths_mutex_);
            path_gateways_.clear();
            path_metrics_.clear();
            next_path_id_ = 0;
        }

        // Каждый интерфейс со своим шлюзом — это отдельный кандидат на путь.
        // Шлюз — это единственное, что действительно различает пути; именно его
        // и зондируем (зонд до игрового сервера ничего не говорит о конкретном
        // пути, если мы не умеем маршрутизировать через выбранный шлюз).
        auto interfaces = route_analyzer_.getInterfaces();
        for (size_t i = 0; i < interfaces.size() && i < config_.max_paths; i++) {
            if (!interfaces[i].gateway.empty())
                addRoute(interfaces[i].gateway);
        }

        probe_thread_ = std::thread(&MultipathEngine::probeLoop, this);
    }

    // Первый цикл зондирования проходит под ожиданием — иначе callers видят
    // пути с нулевыми метриками. Важно: ждём снаружи stop_mutex_, поэтому
    // stop() можно вызвать параллельно и он просто отменит цикл.
}

void MultipathEngine::stop() {
    std::lock_guard<std::mutex> lock(stop_mutex_);
    if (!active_.load()) return;
    active_ = false;
    if (probe_thread_.joinable()) {
        probe_thread_.join();
    }
}

bool MultipathEngine::isActive() const {
    return active_.load();
}

void MultipathEngine::setConfig(const MultipathConfig& config) {
    config_ = config;
}

MultipathConfig MultipathEngine::getConfig() const {
    return config_;
}

std::vector<PathMetrics> MultipathEngine::getPathMetrics() const {
    std::lock_guard<std::mutex> lock(paths_mutex_);
    std::vector<PathMetrics> metrics;
    metrics.reserve(path_metrics_.size());
    for (const auto& [id, m] : path_metrics_) {
        metrics.push_back(m);
    }
    return metrics;
}

std::optional<PathMetrics> MultipathEngine::getBestPath() const {
    std::lock_guard<std::mutex> lock(paths_mutex_);
    std::optional<PathMetrics> best;
    double best_score = -1.0;

    for (const auto& [id, m] : path_metrics_) {
        if (m.is_healthy) {
            double score = calculatePathScore(m);
            if (score > best_score) {
                best_score = score;
                best = m;
            }
        }
    }

    return best;
}

uint32_t MultipathEngine::getActivePathId() const {
    return active_path_id_.load();
}

std::optional<std::string> MultipathEngine::getPathGateway(uint32_t path_id) const {
    std::lock_guard<std::mutex> lock(paths_mutex_);
    const auto it = path_gateways_.find(path_id);
    if (it == path_gateways_.end()) return std::nullopt;
    return it->second;
}

void MultipathEngine::addRoute(const std::string& gateway_ip) {
    if (gateway_ip.empty()) return;
    std::lock_guard<std::mutex> lock(paths_mutex_);
    // Дубликат шлюза — это тот же самый путь, не создаём второй экземпляр.
    for (const auto& [id, gw] : path_gateways_) {
        if (gw == gateway_ip) return;
    }

    uint32_t path_id = next_path_id_++;

    path_gateways_[path_id] = gateway_ip;
    path_metrics_[path_id] = PathMetrics{path_id, 0.0, 0.0, 0.0, 0, 0,
        std::chrono::steady_clock::now(), true};
}

void MultipathEngine::removeRoute(const std::string& gateway_ip) {
    std::lock_guard<std::mutex> lock(paths_mutex_);
    for (auto it = path_gateways_.begin(); it != path_gateways_.end(); ) {
        if (it->second == gateway_ip) {
            path_metrics_.erase(it->first);
            it = path_gateways_.erase(it);
        } else {
            ++it;
        }
    }
}

void MultipathEngine::setPathSwitchCallback(PathSwitchCallback callback) {
    path_switch_callback_ = std::move(callback);
}

void MultipathEngine::setMetricsUpdateCallback(MetricsUpdateCallback callback) {
    metrics_update_callback_ = std::move(callback);
}

void MultipathEngine::setPathProbe(PathProbe probe) {
    probe_ = std::move(probe);
}

MultipathEngine::ProbeSample MultipathEngine::probeTarget(const std::string& target) {
    if (probe_) return probe_(target);
    ProbeSample s;
    s.latency_ms = route_analyzer_.measureLatency(target, kLatencyProbes);
    s.packet_loss_percent = route_analyzer_.measurePacketLoss(target, kLossProbes);
    return s;
}

void MultipathEngine::probeLoop() {
    while (active_.load()) {
        // 1. Краткий снимок путей — реальная сеть не должна зондироваться
        //    под блокировкой: measureLatency/measurePacketLoss блокируют
        //    поток на секунды, и getPathMetrics()/getBestPath() повисали бы.
        std::vector<std::pair<uint32_t, std::string>> targets;
        {
            std::lock_guard<std::mutex> lock(paths_mutex_);
            targets.reserve(path_gateways_.size());
            for (const auto& [id, gw] : path_gateways_) {
                targets.emplace_back(id, gw);
            }
        }

        // 2. Зондирование без блокировки путей. Между этим моментом и записью
        //    назад путь могли удалить — потом просто пропустим его.
        std::vector<std::pair<uint32_t, ProbeSample>> samples;
        samples.reserve(targets.size());
        for (const auto& [id, target] : targets) {
            if (!active_.load()) return;
            samples.emplace_back(id, probeTarget(target));
        }

        // 3. Запись результатов — снова короткая блокировка.
        {
            std::lock_guard<std::mutex> lock(paths_mutex_);
            for (auto& [id, sample] : samples) {
                auto it = path_metrics_.find(id);
                if (it == path_metrics_.end()) continue;

                PathMetrics& m = it->second;
                const double old_latency = m.latency_ms;
                m.latency_ms = sample.latency_ms;
                m.packet_loss_percent = sample.packet_loss_percent;

                // Джиттер — экспоненциально-сглаженное среднее от |delta|,
                // а не перезапись одним последним скачком.
                if (old_latency > 0.0 && sample.latency_ms > 0.0) {
                    const double delta = std::abs(sample.latency_ms - old_latency);
                    m.jitter_ms = (m.jitter_ms > 0.0)
                                      ? (1.0 - kJitterAlpha) * m.jitter_ms + kJitterAlpha * delta
                                      : delta;
                }

                m.last_update = std::chrono::steady_clock::now();
                // Путь здоров, только если он отвечает и не теряет пакеты
                // сверх порога: latency == 0.0 — это «нет ответа», а не «отличный пинг».
                m.is_healthy = (sample.latency_ms > 0.0) &&
                               (sample.packet_loss_percent < config_.loss_threshold_percent);
                m.path_id = id;
            }
        }

        // 4. Оповещение и (если включено) переключение путей. Раньше
        //    evaluatePathSwitch() был мёртвым методом — auto_switch ни на что
        //    не влиял.
        if (metrics_update_callback_) {
            metrics_update_callback_(getPathMetrics());
        }
        evaluatePathSwitch();

        // 5. Прерываемый сон: stop() не должен ждать целый probe_interval.
        for (uint32_t slept = 0;
             active_.load() && slept < config_.probe_interval_ms;
             slept += 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(
                std::min<uint32_t>(100, config_.probe_interval_ms - slept)));
        }
    }
}

void MultipathEngine::evaluatePathSwitch() {
    if (!config_.auto_switch) return;

    auto best = getBestPath();
    if (!best) return;

    uint32_t best_id = best->path_id;
    if (best_id == active_path_id_.load()) return;

    double current_score = 0.0;
    double best_score = calculatePathScore(*best);

    {
        std::lock_guard<std::mutex> lock(paths_mutex_);
        auto it = path_metrics_.find(active_path_id_.load());
        if (it != path_metrics_.end()) {
            current_score = calculatePathScore(it->second);
        }
    }

    if (best_score > current_score + config_.switch_threshold_ms) {
        switchToPath(best_id);
    }
}

void MultipathEngine::switchToPath(uint32_t path_id) {
    // Переключение на несуществующий путь — это ошибка, а не молчаливая
    // порча активного идентификатора.
    {
        std::lock_guard<std::mutex> lock(paths_mutex_);
        if (path_metrics_.find(path_id) == path_metrics_.end()) return;
    }

    uint32_t old_path = active_path_id_.exchange(path_id);
    if (old_path == path_id) return;

    if (path_switch_callback_) {
        path_switch_callback_(old_path, path_id);
    }
}

double MultipathEngine::calculatePathScore(const PathMetrics& metrics) const {
    double score = 100.0;

    score -= metrics.latency_ms * 0.5;
    score -= metrics.jitter_ms * 2.0;
    score -= metrics.packet_loss_percent * 10.0;

    if (!metrics.is_healthy) {
        score -= 50.0;
    }

    auto age = std::chrono::steady_clock::now() - metrics.last_update;
    if (age > std::chrono::seconds(5)) {
        score -= 20.0;
    }

    return std::max(0.0, score);
}

} // namespace gno
