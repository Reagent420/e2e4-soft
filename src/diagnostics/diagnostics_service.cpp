#include "diagnostics/diagnostics_service.h"

#include "core/game_detector.h"
#include "core/route_analyzer.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace gno {

namespace {

// PID процесса по его имени. GameDetector знает process_name запущенной игры,
// но не её PID — поэтому приходится поднимать снимок процессов.
std::optional<std::uint32_t> pidForProcessName(const std::string& process_name) {
    if (process_name.empty()) return std::nullopt;
#ifdef PLATFORM_WINDOWS
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return std::nullopt;

    std::optional<std::uint32_t> found;
    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            const std::wstring name(entry.szExeFile);
            const std::string narrow(name.begin(), name.end());
            if (_stricmp(narrow.c_str(), process_name.c_str()) == 0) {
                found = static_cast<std::uint32_t>(entry.th32ProcessID);
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
#else
    return std::nullopt;
#endif
}

std::optional<std::uint32_t> defaultGameLocator(const std::string& game_id) {
    GameDetector detector;
    detector.detectRunningGames();
    const auto running = detector.getRunningGames();
    const auto match = std::find_if(running.begin(), running.end(),
                                    [&](const GameInfo& info) { return info.name == game_id; });
    if (match == running.end()) return std::nullopt;
    return pidForProcessName(match->process_name);
}

std::optional<std::string> defaultGatewayResolver(const std::string& destination_ip) {
    RouteAnalyzer analyzer;
    const auto route = analyzer.getRouteTo(destination_ip);
    if (!route) return std::nullopt;
    if (route->gateway_ip.empty() || route->gateway_ip == "0.0.0.0") return std::nullopt;
    return route->gateway_ip;
}

} // namespace

DiagnosticsService::DiagnosticsService() : DiagnosticsService(Dependencies{}) {}

DiagnosticsService::DiagnosticsService(Dependencies deps) : deps_(std::move(deps)) {
    if (!deps_.endpoint_observer) deps_.endpoint_observer = std::make_unique<EndpointObserver>();
    if (!deps_.network_sampler) deps_.network_sampler = std::make_unique<NetworkSampler>();
    if (!deps_.probe_client) deps_.probe_client = std::make_unique<ProbeClient>();
    if (!deps_.game_locator) deps_.game_locator = defaultGameLocator;
    if (!deps_.gateway_resolver) deps_.gateway_resolver = defaultGatewayResolver;
}

DiagnosticsService::~DiagnosticsService() = default;

bool DiagnosticsService::isBad(const MetricSummary& metric) noexcept {
    return metric.loss_percent > kBadLossPercent ||
           metric.median_ms > kBadLatencyMs ||
           metric.jitter_ms > kBadJitterMs;
}

std::string DiagnosticsService::formatMetric(const char* label, const MetricSummary& metric) {
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer),
                  "%s: задержка %.0f мс (p95 %.0f), джиттер %.1f мс, потери %.1f%% (%u/%u)",
                  label, metric.median_ms, metric.p95_ms, metric.jitter_ms,
                  metric.loss_percent, metric.received, metric.sent);
    return buffer;
}

DiagnosticResult<DiagnosticReport> DiagnosticsService::run(const ProbeRequest& request,
                                                           const CancellationToken& cancellation) {
    DiagnosticResult<DiagnosticReport> result;

    // 1. Находим процесс игры.
    const auto pid = deps_.game_locator ? deps_.game_locator(request.game_id)
                                        : std::optional<std::uint32_t>{};
    if (!pid || *pid == 0) {
        result.error = DiagnosticError::GameNotDetected;
        result.value.evidence.push_back("Игра не запущена: диагностика путей требует запущенного процесса игры.");
        return result;
    }
    if (cancellation.isCancelled()) {
        result.error = DiagnosticError::Cancelled;
        return result;
    }

    // 2. Находим соединение игры с сервером.
    SamplePlan plan;
    plan.duration_seconds = request.duration_seconds > 0 ? request.duration_seconds : 30;
    // Интервал масштабируется под длительность: маршрутная диагностика не
    // нуждается в ежесекундных пробах, а тестам нужен маленький интервал.
    plan.interval_ms = std::clamp(request.duration_seconds * 100u, 250u, 2000u);
    plan.timeout_ms = 1500;

    const auto observed = deps_.endpoint_observer->observe(
        *pid, std::chrono::seconds(plan.duration_seconds), cancellation);
    if (!observed.ok()) {
        result.error = observed.error;
        result.value.evidence.push_back(
            "Не удалось найти сетевое соединение игрового процесса: возможно, "
            "игра ещё не подключилась к серверу.");
        return result;
    }
    if (observed.value.empty()) {
        result.error = DiagnosticError::EndpointNotObserved;
        result.value.evidence.push_back(
            "У процесса игры нет установленных соединений — диагностика нечего измерять.");
        return result;
    }

    // 3. Выбираем целевую конечную точку: предпочитаем ту, что указана в запросе.
    ObservedEndpoint endpoint = observed.value.front();
    if (!request.endpoint_ip.isUnspecified()) {
        const auto match = std::find_if(observed.value.begin(), observed.value.end(),
                                        [&](const ObservedEndpoint& candidate) {
                                            return candidate.ip == request.endpoint_ip;
                                        });
        if (match != observed.value.end()) endpoint = *match;
    }

    // 4. Измеряем шлюз (первый хоп) — если получится.
    MetricSummary gateway_metrics;
    bool have_gateway = false;
    const auto gateway_ip = deps_.gateway_resolver(endpoint.ip.toString());
    if (gateway_ip) {
        SampleTarget gateway_target;
        gateway_target.ip = Ipv4Address::parse(*gateway_ip).value_or(Ipv4Address{});
        gateway_target.port = 0; // ICMP не использует порты
        gateway_target.protocol = TransportProtocol::Udp;
        if (!gateway_target.ip.isUnspecified()) {
            const auto gateway_result = deps_.network_sampler->sample(gateway_target, plan, cancellation);
            if (gateway_result.ok()) {
                gateway_metrics = gateway_result.value;
                have_gateway = true;
            }
        }
    }

    if (cancellation.isCancelled()) {
        result.error = DiagnosticError::Cancelled;
        return result;
    }

    // 5. Измеряем прямой маршрут до сервера игры.
    SampleTarget direct_target;
    direct_target.ip = endpoint.ip;
    direct_target.port = endpoint.port;
    direct_target.protocol = endpoint.protocol;
    const auto direct_result = deps_.network_sampler->sample(direct_target, plan, cancellation);

    DiagnosticReport report;
    report.game_id = request.game_id;
    report.endpoint = endpoint;
    if (have_gateway) report.gateway = gateway_metrics;

    if (!direct_result.ok()) {
        // Недостаточно данных по прямому маршруту — диагноз невозможен.
        report.outcome = DiagnosticOutcome::InsufficientData;
        report.confidence = ConfidenceLevel::Low;
        report.direct = direct_result.value;
        report.evidence.push_back(
            "Прямой маршрут: слишком мало ответов от сервера игры, чтобы сделать вывод.");
        if (have_gateway) report.evidence.push_back(formatMetric("Шлюз", gateway_metrics));
        result.value = report;
        result.error = DiagnosticError::None;
        return result;
    }
    report.direct = direct_result.value;
    report.evidence.push_back(formatMetric("Сервер игры", direct_result.value));
    if (have_gateway) report.evidence.push_back(formatMetric("Шлюз", gateway_metrics));

    // 6. Сравниваем с эталонными публичными адресами.
    DiagnosticResult<ProbeMeasurement> probe_result = deps_.probe_client->measure(request, cancellation);
    if (probe_result.ok()) {
        report.candidates.push_back(probe_result.value);
        report.evidence.push_back(formatMetric(probe_result.value.probe_region.c_str(),
                                              probe_result.value.client_to_probe));
    }

    // 7. Классификация.
    // Сначала проверяем локальную сеть: шлюз плох — корень проблемы у пользователя.
    if (have_gateway && isBad(gateway_metrics)) {
        report.outcome = DiagnosticOutcome::LocalNetworkProblem;
        report.confidence = ConfidenceLevel::High;
        report.evidence.push_back(
            "Шлюз/роутер отвечает плохо: проблема в локальной сети, а не в маршруте "
            "до сервера игры. Проверьте кабель/Wi-Fi и перезагрузите роутер.");
        result.value = report;
        result.error = DiagnosticError::None;
        return result;
    }

    if (!isBad(direct_result.value)) {
        report.outcome = DiagnosticOutcome::NoImprovementFound;
        report.confidence = have_gateway ? ConfidenceLevel::High : ConfidenceLevel::Medium;
        report.evidence.push_back(
            "Маршрут до сервера игры в норме — улучшать нечего. Если лаги всё равно "
            "ощущаются, причина, скорее всего, в самой игре или её серверах.");
        result.value = report;
        result.error = DiagnosticError::None;
        return result;
    }

    // Прямой маршрут плох. Сравниваем с эталоном: страдает именно этот маршрут
    // или связь в целом?
    if (probe_result.ok() && !isBad(probe_result.value.client_to_probe)) {
        report.outcome = DiagnosticOutcome::ImprovementLikely;
        report.confidence = ConfidenceLevel::Medium;
        report.evidence.push_back(
            "До публичных эталонов задержка в норме, а до сервера игры — высокая: "
            "страдает именно маршрут до игровых серверов провайдером. GNO не строит "
            "VPN/relay, поэтому перенос трафика здесь невозможен — имеет смысл "
            "обратиться к провайдеру или выбрать другой регион сервера в настройках игры.");
        result.value = report;
        result.error = DiagnosticError::None;
        return result;
    }

    if (probe_result.ok() && isBad(probe_result.value.client_to_probe)) {
        report.outcome = DiagnosticOutcome::LocalNetworkProblem;
        report.confidence = ConfidenceLevel::Medium;
        report.evidence.push_back(
            "Плохо ходит трафик до любых адресов, а не только до сервера игры: "
            "проблема в канале в целом. Проверьте загрузку канала ( кто-то качает "
            "обновления), кабель и роутер.");
        result.value = report;
        result.error = DiagnosticError::None;
        return result;
    }

    // Эталон недоступен (например, ICMP заблокирован) — не с чем сравнить.
    report.outcome = DiagnosticOutcome::InsufficientData;
    report.confidence = ConfidenceLevel::Low;
    report.evidence.push_back(
        "Сравнить маршрут не с чем: публичные эталоны не ответили. Диагноз по "
        "прямому маршруту есть, но отделить локальную проблему от маршрута "
        "провайдера невозможно.");
    result.value = report;
    result.error = DiagnosticError::None;
    return result;
}

} // namespace gno
