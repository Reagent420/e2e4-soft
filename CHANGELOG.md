# Changelog

All notable changes to GNO are documented in this file.

## [3.0.0] — Commercial Release

### Added
- Transactional remediation engine: 10 whitelisted actions with backup → verify → rollback
- CS2 matchmaking ping filter (`mm_dedicated_search_maxping`) via safe engine
- Game Mode and Mouse Acceleration as engine actions (unified path)
- Fine Tuning page: 28 curated registry tweaks in 7 categories
- FPS Boost page: timer resolution 0.5ms, RAM cleaner, service control, startup manager
- Server Map: interactive world map with live ICMP probing, region filter, best-node detection
- Launch diagnostics: anti-cheat services, VC++ redistributables, disk, admin rights
- Knowledge base: 25+ curated problems with auto-fix integration and search
- Plain-language report generator ("What's wrong / What we do / What you do / What we don't")
- Scheduler: evening quality snapshot, morning scan, configurable time
- JSON report export with atomic write
- CSV session history export with quality scores
- Score trend chart (QtCharts) on History page
- Region advisor after network diagnostics
- Per-game alert thresholds (VALORANT 50ms/1%, CS2 60/1.5%, PUBG 100/3%)
- Tray quality score tooltip + degradation alerts
- Profiles export/import (.gnoprofile format)
- i18n framework (Russian/English) with language selector

### Changed
- Cyber theme as default dark theme with neon accent system
- All UI text encoded in UTF-8 (CP1251 sources transcoded)
- Version unified from single source (theme::APP_VERSION)
- Lazy page initialization for faster application startup

### Fixed
- Serializer bug: nested registry values losing `existed` flag on load
- Cross-thread data race in server map probe writes
- Autopilot UI freeze during game profile application (moved to worker thread)
- Chronological ordering of transaction history (was ID-sorted)
- DNS netsh command using adapter GUID instead of friendly name
- Memory leak: SpeedTest instances created per-hop in MTR utility
- Sidebar labels double-encoded Cyrillic replaced with correct UTF-8
- Transaction limit raised to 12 to support expanded action set

### Security
- All system modifications require snapshot before write
- Failed verification triggers automatic rollback
- Whitelist enforcement: no out-of-catalog registry writes possible

## [2.x] — Development Releases

### [2.7.0]
- Single-path engine: fps_optimizer direct writes replaced with bridge calls
- No more dual write paths for any action

### [2.6.x]
- Version synchronization across all source files
- Console banner reads from theme::APP_VERSION

### [2.5.0]
- Stability release consolidating all v2.x features
- Transaction limit raised to 12

### [2.4.0]
- FPS Boost page: timer resolution, RAM cleaner, services, startup programs

### [2.3.0]
- Tweak registry framework: 24 options in 7 categories
- History retention (500 records) + CSV export
- Engine actions: GameMode, MouseAccel added to whitelist

### [2.2.x]
- Server map visual redesign: wireframe continents, pulse animation
- Text encoding fixes (CP1251 sources transcoded to UTF-8)

### [2.1.0]
- Score trend chart on History page
- Region advisor after diagnostics
- Editable alert thresholds per game
- DNS cache in utilities

### [2.0.0]
- CS2 matchmaking ping filter via safe engine
- Per-game alert thresholds
- Honest capability matrix update

## [1.x] — Foundation

### [1.9.x]
- Profile export/import (.gnoprofile)
- Audit fixes: chronological history, netsh friendly name, MTR leak

### [1.8.0]
- Scheduler, JSON report export, fullscreen_opt via engine

### [1.7.0]
- Network Utilities expansion: MTR-lite, Wi-Fi analyzer, NAT type, MTU probe, bufferbloat

### [1.6.0]
- Cyber theme, server map 2.0, autopilot profiles, tray score

### [1.5.0]
- Safe remediation engine ported from C# reference implementation
