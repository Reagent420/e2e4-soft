# GNO — macOS Port Roadmap

## Current Status
Windows version: v2.6.0 (stable, fully tested)
macOS version: NOT YET FUNCTIONAL — infrastructure phase

## Architecture

```
src/
├── platform/
│   ├── windows/     # Win32 implementations
│   ├── macos/       # macOS implementations (TODO)
│   └── linux/       # Linux implementations (future)
├── core/            # Cross-platform logic (shared)
├── remediation/     # Mostly Windows-only
├── monitoring/      # Partially cross-platform
├── optimization/    # Windows-only
└── ui/              # Qt6 cross-platform
```

## What transfers to macOS WITHOUT changes
- Qt6 UI framework (all widgets)
- Core logic: profile_engine, alert_thresholds, network_stats,
  capability_matrix, plain_language, server_map_model,
  report_exporter, autopilot_plan
- JSON persistence (json_persistence)
- Session history (data model)
- Problem database (knowledge base)
- Game profiles (JSON)

## What needs macOS implementation
| Module | Windows API | macOS equivalent |
|--------|------------|-----------------|
| DNS | netsh | networksetup |
| Ping | IcmpSendEcho | raw sockets / process |
| Traceroute | IcmpSendEcho | traceroute command |
| Power Plan | powercfg | pmset |
| Services | SCM | launchctl |
| Mouse Accel | HKCU Registry | defaults write |
| Timer Resolution | NtSetTimerResolution | dispatch_source |
| RAM Cleaner | EmptyWorkingSet | mach_vm |
| Startup Programs | HKCU Run keys | LaunchAgents |
| Game Detection | Win32 Process | NSWorkspace |
| System Audit | Win32 | sysctl / ioreg |

## What is Windows-only (excluded on macOS)
- Game DVR / Fullscreen Optimizations
- CS2 maxping via Steam registry
- Mouse acceleration via HKCU
- Win32PrioritySeparation
- USB Selective Suspend
- Power Throttling
- Delivery Optimization service

## Build on macOS
```bash
brew install cmake ninja qt6
mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

## Phases
1. **PAL Layer**: Abstract interfaces for OS-specific operations
2. **Core Port**: Compile cross-platform modules on macOS
3. **UI Port**: Qt6 widgets work as-is, minor styling adjustments
4. **Feature Parity**: Implement macOS equivalents for safe tweaks
5. **Packaging**: .app bundle, codesign, notarization, DMG