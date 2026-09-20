<div align="center">

# E2E4 Soft — Game Network Optimizer

**Advanced Network Diagnostics & System Optimization for Competitive Gaming**

[![Version](https://img.shields.io/badge/version-3.1.0-blue.svg)](https://github.com/Reagent420/e2e4-soft/releases)
[![Tests](https://img.shields.io/badge/tests-187%20passing-brightgreen.svg)]()
[![Platform](https://img.shields.io/badge/platform-Windows-lightgrey.svg)]()
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

*Understand your network. Fix safely. Play without lag.*

</div>

---

## What is GNO?

GNO is a comprehensive network diagnostics and system optimization tool designed specifically for competitive gamers. It answers three questions: *What's wrong with my network?*, *Why won't my game launch?*, and *What can be safely fixed?*

Unlike generic "boosters", GNO uses a **transactional engine** — every system change is backed up, verified after application, and fully reversible with one click.

## Key Features

### 🌐 Network Diagnostics
Real-time ping, jitter, and packet loss measurement against servers in 20+ games with a proprietary Quality Score (0–100). Advanced mode includes DNS resolver latency, traceroute with hop-by-hop analysis, IPv4/IPv6 validation, and interface enumeration.

### 🎮 Launch Diagnostics
Comprehensive pre-launch checks: anti-cheat services (BattlEye, Vanguard, EAC), Visual C++ Redistributables, disk space, administrator privileges. Each finding includes severity rating, plain-language explanation, and one-click auto-fix where safe.

### 🔧 Transactional Engine
The only optimization tool where every change follows: **backup → apply → verify → rollback**. Ten whitelisted actions covering DNS, MTU, TCP tuning, power plans, Game DVR, fullscreen optimizations, process priority, CS2 matchmaking filter, Game Mode, and mouse acceleration.

### 🗺️ Server Map
Interactive world map with live ICMP probing of game server regions. Color-coded latency grades, best-server detection, region filtering, and auto-refresh scheduling.

### 📚 Knowledge Base
Curated database of common and complex gaming problems organized by title: symptom → cause → solution. Simple issues resolved via integrated auto-fix; complex ones include step-by-step guides.

### 📊 Analytics
Score trend charts over session history, per-game alert thresholds, weekly health aggregation, CSV/PNG/JSON report export.

### 📄 Report Export
Diagnostic reports and session history export to **Markdown, HTML and PDF** (A4, 300 dpi) from the UI — the PDF is generated locally, nothing is uploaded.

### 🔄 Signed Updates
Built-in update check against GitHub Releases. A new build is accepted **only if its SHA-256 matches** the digest published with the release; if no digest is available the download is refused rather than installed on trust. Verified files are saved to disk — installation stays in your hands.

### 🗒️ Session Log
Every run writes a rotating log to `%APPDATA%\GNO\logs` (7 days kept), including Qt's own warnings. When something goes wrong on a machine you don't control, the log is the evidence.

---

## Safety Model

Every modification follows a strict protocol:

```
┌──────────┐    ┌─────────┐    ┌─────────┐    ┌──────────┐
│ BACKUP   │ →  │ APPLY   │ →  │ VERIFY  │ →  │ ROLLBACK │
│ snapshot │    │ change  │    │ result  │    │ if fail  │
└──────────┘    └─────────┘    └─────────┘    └──────────┘
```

Only whitelisted registry keys and network parameters are modified. Game files, anti-cheat software, and drivers are never touched.

## Honest Limitations

- Cannot reduce ping beyond what your ISP provides
- Cannot filter matchmaking from outside the game client
- Cannot bypass anti-cheat systems
- Is not a VPN or proxy

These limitations are stated openly because trust matters more than marketing.

---

## Installation

Download `GNO-v3.1.0.zip` from [Releases](https://github.com/Reagent420/e2e4-soft/releases), extract, run `GNO.exe`. Qt libraries included.

For full functionality, run as Administrator.

## Building from Source

Requirements: MSYS2 + MinGW64, CMake ≥ 3.20, Qt6 (Widgets, Charts, Network)

```bash
pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake \
         mingw-w64-x86_64-ninja mingw-w64-x86_64-qt6

git clone https://github.com/Reagent420/e2e4-soft.git
cd e2e4-soft && mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build .
./bin/GNO-tests.exe    # 187 cases / 1014 assertions
```

SQLite is vendored in `third_party/sqlite` (see its README) — no network access is
needed at configure time. To assemble a runnable folder from a build tree:

```bash
./scripts/deploy.sh                # lay out the Qt runtime next to the exe
./scripts/smoke-test.sh            # verify GNO.exe actually starts
```

## Command Line Interface

The console build is `GNO-console.exe` (not the GUI `GNO.exe`):

```text
GNO-console --target 1.1.1.1 --ping 10
GNO-console --multipath            # per-path probing and route selection
GNO-console --boost --game "Counter-Strike 2"
GNO-console --speedtest
GNO-console --dns-benchmark
GNO-console --watch
GNO-console --help                 # full flag list
```

---

## Technical Stack

| Component | Technology |
|---|---|
| Language | C++20 |
| UI Framework | Qt 6 (Widgets, Charts) |
| Networking | Qt 6 Network (update check), Win32 IP Helper / Winsock (probing) |
| Storage | SQLite 3.49.1 (vendored) + JSON files |
| Build System | CMake + Ninja |
| Testing | doctest |
| Compiler | MinGW GCC 16.x |
| Target | Windows 10/11 x64 |

## Project Stats

| Metric | Value |
|---|---|
| Source files | 161 |
| Lines of code | ~34,000 |
| Test cases | 187 |
| Test assertions | 1014 |
| Whitelisted actions | 10 |
| Registry tweaks | 28 |
| Supported games | 23 |
| Knowledge base entries | 25+ |

---

## License

MIT — see [LICENSE](LICENSE)

---

<div align="center">

**Reagent Network Service e2E4**

Built with Qt6 · Offline-first · No telemetry

</div>
