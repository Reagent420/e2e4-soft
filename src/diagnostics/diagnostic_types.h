#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gno {

enum class DiagnosticError {
    None,
    PermissionDenied,
    UnsupportedCapability,
    GameNotDetected,
    EndpointNotObserved,
    EndpointNotAllowlisted,
    ProbeUnavailable,
    Timeout,
    Cancelled,
    InsufficientResponses,
    MalformedResponse,
    InternalFailure
};

enum class DiagnosticOutcome {
    ImprovementLikely,
    NoImprovementFound,
    LocalNetworkProblem,
    InsufficientData
};

enum class ConfidenceLevel { Low, Medium, High };
enum class TransportProtocol { Tcp, Udp };

template <typename T>
struct DiagnosticResult {
    T value{};
    DiagnosticError error = DiagnosticError::InternalFailure;

    bool ok() const noexcept { return error == DiagnosticError::None; }
};

class CancellationToken {
public:
    CancellationToken() = default;

    bool isCancelled() const noexcept;

private:
    explicit CancellationToken(std::shared_ptr<const std::atomic<bool>> state);

    std::shared_ptr<const std::atomic<bool>> state_;

    friend class CancellationSource;
};

class CancellationSource {
public:
    CancellationSource();

    // Copies share cancellation state. Moves transfer it; a moved-from source
    // is inert, returning an uncancelled token and ignoring cancel().
    CancellationSource(const CancellationSource&) = default;
    CancellationSource& operator=(const CancellationSource&) = default;
    CancellationSource(CancellationSource&&) noexcept = default;
    CancellationSource& operator=(CancellationSource&&) noexcept = default;

    CancellationToken token() const;
    void cancel() noexcept;

private:
    std::shared_ptr<std::atomic<bool>> state_;
};

class Ipv4Address {
public:
    // 0.0.0.0 is the explicit unspecified state for safe default reports and targets.
    Ipv4Address() = default;

    static std::optional<Ipv4Address> parse(std::string_view text) noexcept;

    std::string toString() const;
    const std::array<uint8_t, 4>& bytes() const noexcept { return bytes_; }
    bool isUnspecified() const noexcept { return bytes_ == std::array<uint8_t, 4>{}; }

    friend bool operator==(const Ipv4Address& left, const Ipv4Address& right) noexcept {
        return left.bytes_ == right.bytes_;
    }

    friend bool operator!=(const Ipv4Address& left, const Ipv4Address& right) noexcept {
        return !(left == right);
    }

private:
    explicit Ipv4Address(std::array<uint8_t, 4> bytes) : bytes_(bytes) {}

    std::array<uint8_t, 4> bytes_{};
};

struct ObservedEndpoint {
    Ipv4Address ip;
    uint16_t port = 0;
    TransportProtocol protocol = TransportProtocol::Udp;
    uint32_t owner_pid = 0;
    uint64_t observed_packets = 0;
};

struct SampleTarget {
    Ipv4Address ip;
    uint16_t port = 0;
    TransportProtocol protocol = TransportProtocol::Udp;
};

struct SamplePlan {
    uint32_t duration_seconds = 30;
    uint32_t interval_ms = 1000;
    uint32_t timeout_ms = 1000;
};

struct MetricSummary {
    uint32_t sent = 0;
    uint32_t received = 0;
    double median_ms = 0.0;
    double p95_ms = 0.0;
    double jitter_ms = 0.0;
    double loss_percent = 0.0;
};

struct ProbeRequest {
    std::string game_id;
    Ipv4Address endpoint_ip;
    uint16_t endpoint_port = 0;
    TransportProtocol protocol = TransportProtocol::Udp;
    uint32_t duration_seconds = 30;
};

struct ProbeMeasurement {
    std::string probe_region;
    MetricSummary client_to_probe;
    MetricSummary probe_to_game;
};

struct DiagnosticReport {
    DiagnosticOutcome outcome = DiagnosticOutcome::InsufficientData;
    ConfidenceLevel confidence = ConfidenceLevel::Low;
    std::string game_id;
    ObservedEndpoint endpoint;
    MetricSummary gateway;
    MetricSummary direct;
    std::vector<ProbeMeasurement> candidates;
    std::vector<std::string> evidence;
    static constexpr bool network_settings_changed = false;
};

} // namespace gno
