#pragma once

#include "diagnostic_types.h"
#include "endpoint_observer.h"
#include "network_sampler.h"
#include "probe_client.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <cstdint>

namespace gno {

// Собирает полный диагноз маршрута до серверов игры: находит процесс,
// определяет его соединение, измеряет шлюз и прямой маршрут, сравнивает
// с эталонными публичными адресами и делает честный вывод.
class DiagnosticsService {
public:
    // Поиск PID запущенной игры по её game_id.
    using GameLocator = std::function<std::optional<std::uint32_t>(const std::string& game_id)>;
    // IP шлюза для назначения (по умолчанию — RouteAnalyzer::getRouteTo).
    using GatewayResolver = std::function<std::optional<std::string>(const std::string& destination_ip)>;

    struct Dependencies {
        std::unique_ptr<IEndpointObserver> endpoint_observer;
        std::unique_ptr<INetworkSampler> network_sampler;
        std::unique_ptr<IProbeClient> probe_client;
        GameLocator game_locator;
        GatewayResolver gateway_resolver;
    };

    DiagnosticsService();
    explicit DiagnosticsService(Dependencies deps);
    ~DiagnosticsService();

    DiagnosticResult<DiagnosticReport> run(const ProbeRequest& request,
                                           const CancellationToken& cancellation);

    // Пороги «плохого» соединения — те же, что в plain_language_report.
    static constexpr double kBadLatencyMs = 80.0;
    static constexpr double kBadLossPercent = 2.0;
    static constexpr double kBadJitterMs = 15.0;

private:
    static bool isBad(const MetricSummary& metric) noexcept;
    static std::string formatMetric(const char* label, const MetricSummary& metric);

    Dependencies deps_;
};

} // namespace gno
