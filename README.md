# Game Network Optimizer (GNO)

Professional game network optimization tool for reducing latency, packet loss, and jitter in online games.

## Features

### Multipath Routing
- Intelligent multi-route selection
- Automatic route switching on failure
- Real-time latency monitoring per path
- Load balancing between routes

### FPS Boost
- Disable Game DVR
- Disable fullscreen optimizations
- Disable mouse acceleration
- Optimize power plan
- Set game process priority
- Optimize virtual memory

### Network Monitoring
- Real-time ping monitoring
- Packet loss detection
- Jitter calculation
- Historical data visualization

### Game Support
- 30+ popular games
- Automatic game detection
- Game-specific optimization profiles
- Regional server selection

## Building

### Prerequisites
- CMake 3.20+
- Qt6 (Widgets, Charts, Network)
- C++20 compatible compiler

### Build Commands
```bash
mkdir build
cd build
cmake ..
cmake --build .
```

## Project Structure

```
src/
├── core/                    # Core networking
│   ├── route_analyzer.h/cpp    # Route analysis
│   ├── multipath_engine.h/cpp  # Multipath routing
│   ├── packet_capture.h/cpp    # Packet capture
│   ├── game_detector.h/cpp     # Game detection
│   └── network_utils.h/cpp     # Network utilities
├── optimization/            # FPS Boost
│   ├── fps_optimizer.h/cpp     # FPS optimization
│   └── system_tweaks.h/cpp     # System tweaks
├── monitoring/              # Network monitoring
│   ├── ping_monitor.h/cpp      # Ping monitoring
│   ├── packet_loss_monitor.h/cpp # Packet loss
│   ├── jitter_calculator.h/cpp # Jitter calculation
│   └── stats_collector.h/cpp   # Statistics
└── ui/                      # User interface
    ├── main_window.h/cpp       # Main window
    ├── game_selector.h/cpp     # Game selection
    ├── monitoring_panel.h/cpp  # Monitoring UI
    └── settings_dialog.h/cpp   # Settings
```

## How It Works

1. **Route Analysis**: Analyzes all possible routes to game servers
2. **Multipath Engine**: Selects and maintains optimal routes
3. **Real-time Monitoring**: Tracks latency, packet loss, jitter
4. **FPS Boost**: Optimizes system settings for better performance
5. **Auto-switching**: Automatically switches routes on failure

## Comparison with Competitors

| Feature | GNO | ExitLag | GearUP |
|---------|-----|---------|--------|
| Multipath Routing | ✓ | ✓ | ✗ |
| FPS Boost | ✓ | ✓ | ✗ |
| Multi-platform | ✓ | ✗ | ✓ |
| Real-time Monitoring | ✓ | ✗ | ✗ |
| Game Auto-detect | ✓ | ✓ | ✓ |
| Open Source | ✓ | ✗ | ✗ |

## License

MIT License
