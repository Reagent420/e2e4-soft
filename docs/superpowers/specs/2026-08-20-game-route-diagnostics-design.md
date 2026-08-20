# Game Route Diagnostics Design

**Date:** 2026-08-20

**Status:** Approved for planning

## Goal

Build a safe Windows and macOS desktop diagnostic that detects a running game, observes its remote endpoints, measures the current connection, compares it with measurements from regional probe servers, and explains whether an alternative route is likely to improve latency, jitter, or packet loss.

The first version is diagnostic only. It must not install a VPN, create a tunnel, change DNS, modify routes, terminate processes, alter registry or `sysctl` settings, or require administrator/root access.

## Product Promise

The application answers one question: "Is there credible evidence that this game's connection could benefit from an alternative route?"

It reports one of four outcomes:

- **Improvement likely:** at least one probe region has a stable, material estimated advantage.
- **No improvement found:** the direct route is as good as or better than all measured candidates.
- **Local network problem:** the path to the local gateway already exhibits material jitter or packet loss.
- **Insufficient data:** the available endpoints or protocols do not support enough valid measurements.

The result is an estimate, not a guarantee. The UI must show an expected range, a confidence level, and the evidence used to reach the conclusion.

## Non-Goals

- No packet forwarding or traffic interception.
- No WireGuard, Wintun, `utun`, WFP callout, Network Extension, or privileged helper in this release.
- No automatic network or operating-system optimization.
- No user accounts, payments, subscriptions, or cloud control plane.
- No claim that changing DNS improves in-game latency after a connection is established.
- No custom cryptography.

## Supported Platforms

- Windows 10 and Windows 11, x86-64.
- macOS 13 or newer on Apple Silicon and Intel.
- A paid Apple Developer account is not required for local builds. Public macOS distribution, Developer ID signing, and notarization remain a later release concern.

The existing Qt 6/C++17 user interface and shared domain code remain the application foundation. Platform-specific endpoint observation and network measurement live behind narrow interfaces.

## Architecture

### Desktop client

The desktop application contains five components:

1. **GameDetector** identifies a supported running game and its process identifier.
2. **EndpointObserver** maps that process to active remote IP addresses, ports, and transport protocols.
3. **NetworkSampler** measures the local gateway, direct endpoint, and configured probe regions.
4. **ProbeClient** requests bounded measurements from the regional probe service.
5. **DiagnosticEngine** combines measurements into an outcome, confidence level, and human-readable evidence.

All components publish immutable result objects. UI code consumes those objects and does not call platform APIs directly.

### Platform adapters

Windows endpoint observation uses the IP Helper APIs, including the extended TCP and UDP tables, and maps socket owner PIDs to the detected game.

macOS endpoint observation uses public `libproc` process and file-descriptor APIs for processes owned by the current user. If macOS denies endpoint information, the application reports the permission limitation and offers manual selection from the supported game's known server regions. It does not invoke shell pipelines or parse `lsof` output.

Windows active ICMP measurements use the IP Helper ICMP APIs. macOS measurements use sockets and fixed-path tools launched with argument arrays only when no public in-process API is adequate; no shell is involved. Every active technique is best-effort because many game servers reject ICMP or unsolicited transport probes.

### Regional probe service

The initial deployment requires at least one Linux probe. Each probe exposes a small HTTPS API and runs the same bounded sampler used by the client where practical.

The service accepts only:

- a known `game_id`;
- an IP address contained in that game's bundled allowlisted prefixes;
- a port and protocol allowed by that game entry;
- a fixed diagnostic duration not exceeding 30 seconds.

It rejects hostnames, URLs, private/link-local/loopback/multicast addresses, unknown ports, oversized requests, and destinations outside the catalog. Per-client and global rate limits prevent the probe from becoming a scanner or denial-of-service amplifier.

The probe returns aggregate sample counts, median and p95 latency, jitter, packet loss, timestamps, and explicit failure reasons. It never returns packet contents and does not retain diagnostic requests after operational logs expire. Operational logs exclude game endpoint IPs and client-provided identifiers.

## Data Flow

1. The user starts a diagnostic session.
2. `GameDetector` finds a supported running game.
3. `EndpointObserver` observes candidate remote endpoints for a short stabilization window.
4. The client filters candidates through the bundled game catalog and selects the endpoints with sustained traffic.
5. `NetworkSampler` samples the local gateway, direct game endpoint where possible, and each configured regional probe for 30 seconds.
6. `ProbeClient` asks each reachable probe to measure its path to the same allowed game endpoint during the same time window.
7. `DiagnosticEngine` evaluates the direct path, local segment, and each candidate path.
8. The UI displays the outcome, evidence, confidence, and limitations. No system state is changed.

## Measurement Model

For each metric, the diagnostic uses median and p95 values instead of a single ping. Jitter is calculated from consecutive valid latency samples. Packet loss includes only probes for which a response is expected by the selected technique.

The estimated candidate latency is:

```text
candidate RTT = client-to-probe RTT + probe-to-game RTT + tunnel overhead
```

The first version models tunnel overhead as a displayed range of 2–5 ms. The report must state that route asymmetry, game protocol behavior, and real tunnel processing can make the eventual result differ from the estimate.

An improvement is material only when the candidate median is both:

- at least 8 ms lower than the direct median; and
- at least 15 percent lower than the direct median.

The candidate must not materially worsen p95 latency, jitter, or loss. A lower median with unstable tail latency is not classified as an improvement.

### Confidence

- **High:** at least 20 valid samples exist for the direct path and both candidate segments, and at least 80 percent of paired estimates show a material advantage.
- **Medium:** at least 10 valid samples exist for all three paths, and at least 70 percent of paired estimates show an advantage.
- **Low:** the result depends on fallbacks, incomplete endpoint responses, or fewer valid samples.
- If the minimum medium-confidence sample requirements are not met, the overall outcome is **Insufficient data**, though low-confidence observations may still be shown as context.

### Local network diagnosis

The client measures the default gateway separately. Gateway packet loss above 1 percent or median jitter above 5 ms marks the local segment as suspect. Where public platform APIs permit it, the report also includes interface type, negotiated link rate, and Wi-Fi signal; inability to access those values does not fail the session.

## Game Catalog

The catalog defines stable game IDs, executable identities, allowed server prefixes, permitted ports and protocols, and display regions. The first release bundles the catalog with the application. It does not download unsigned catalog updates.

Executable matching uses both process name and executable path. Imported user profiles cannot add probe destinations. A future signed catalog updater is outside this release.

## User Interface

The primary workflow is a single diagnostic session:

1. Select or automatically detect a game.
2. Start a 30-second measurement.
3. Show live progress for local, direct, and probe measurements.
4. Present the final outcome and evidence.

The result view includes:

- detected game, endpoint, and inferred region;
- direct median/p95 latency, jitter, and loss;
- local gateway health;
- candidate probe regions and estimated ranges;
- confidence and sample counts;
- a plain-language explanation of why the conclusion was selected;
- a visible statement that no network settings were changed.

Existing UI actions that claim to apply DNS, multipath, FPS, route, or system optimizations must be hidden or visibly marked unavailable in this diagnostic release. Success messages must only follow verified operations.

## Error Handling

Each component returns a typed error category: permission denied, unsupported platform capability, game not detected, endpoint not observed, endpoint not allowlisted, probe unavailable, timeout, insufficient responses, malformed response, or internal failure.

A failed probe does not fail the complete session. The engine uses remaining probes and lowers confidence. If the direct endpoint rejects active probes, the UI explains the limitation and avoids an acceleration verdict unless an equivalent passive measurement is available.

Cancellation stops new probes, waits for active tasks to finish within a bounded deadline, and releases all threads. No background worker may be detached from its owning object.

## Security and Privacy

- The client performs no privileged operations.
- Platform commands are never assembled as shell strings.
- All probe traffic uses TLS with normal certificate and hostname verification.
- Probe responses have strict size limits and schema validation.
- The client sends no process path, username, packet contents, or unrelated connection list to a probe.
- The probe receives only the selected game ID and allowlisted endpoint needed for its measurement.
- Logs exclude secrets and packet contents and redact endpoint addresses from routine operational events.
- Imported files have explicit byte and record limits and use a real structured parser rather than substring-based parsing.

## Testing Strategy

The domain engine is tested with deterministic recorded scenarios:

- healthy direct route with no beneficial candidate;
- stable candidate with a material median improvement;
- lower candidate median but worse p95 latency;
- local gateway loss and jitter;
- contradictory probes;
- blocked ICMP and partial fallback data;
- insufficient sample counts;
- malicious or out-of-catalog probe request;
- oversized and malformed probe response;
- cancellation during an active measurement.

Platform adapters have integration tests for process-to-endpoint ownership and permission failures. Probe API tests verify allowlists, address classification, rate limits, deadlines, response limits, and log redaction.

Windows and macOS CI must build the diagnostic client. Linux CI builds and tests the probe service. Sanitizer builds run for shared C++ code where supported. Release readiness requires clean unit and integration tests on both client platforms; the existing platform compilation failures must be fixed before feature work is considered complete.

## Delivery Slices

1. **Safe cross-platform foundation:** repair Windows/macOS/Linux compilation, replace unsafe parsing and detached workers, and define typed measurement interfaces.
2. **Local diagnostic engine:** game detection, endpoint observation, gateway/direct sampling, deterministic scoring, and report UI without any remote probe.
3. **Regional probe:** bounded Linux service, allowlisted game catalog, TLS API, rate limiting, and client integration.
4. **Cross-platform validation:** Windows and macOS integration tests, recorded network scenarios, packaging for local use, and documentation of measurement limitations.

Each slice produces a runnable, independently testable deliverable. Tunnel implementation begins only in a later design after diagnostic evidence demonstrates that candidate routes exist.

## Acceptance Criteria

- The same source tree builds the diagnostic client on supported Windows and macOS targets.
- A user can complete a diagnostic session without administrator/root privileges.
- No first-version workflow modifies DNS, routes, process state, registry, `sysctl`, firewall state, or network interfaces.
- The application reports exactly one defined outcome with evidence and confidence.
- Improvement is never reported without meeting both threshold and sample-consistency rules.
- Probe requests cannot target destinations outside the bundled game catalog.
- Closing or cancelling a session leaves no background worker accessing destroyed state.
- The UI accurately reports unavailable capabilities and failed operations.
