cmake_minimum_required(VERSION 3.20)
project(GameNetworkOptimizer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

option(GNO_BUILD_UI "Build with Qt UI" ON)
option(GNO_BUILD_TESTS "Build tests" OFF)

find_package(Qt6 COMPONENTS Widgets Charts Network QUIET)
if(NOT Qt6_FOUND)
    find_package(Qt5 COMPONENTS Widgets Charts Network REQUIRED)
endif()

find_package(Threads REQUIRED)

# Platform detection
if(WIN32)
    add_definitions(-DPLATFORM_WINDOWS)
    set(PLATFORM_SOURCES
        src/core/platform_windows.cpp
        src/optimization/fps_optimizer_windows.cpp
    )
elseif(APPLE)
    add_definitions(-DPLATFORM_MACOS)
    set(PLATFORM_SOURCES
        src/core/platform_macos.cpp
        src/optimization/fps_optimizer_macos.cpp
    )
else()
    add_definitions(-DPLATFORM_LINUX)
    set(PLATFORM_SOURCES
        src/core/platform_linux.cpp
        src/optimization/fps_optimizer_linux.cpp
    )
endif()

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/core/route_analyzer.cpp
    src/core/multipath_engine.cpp
    src/core/packet_capture.cpp
    src/core/game_detector.cpp
    src/core/network_utils.cpp
    src/optimization/fps_optimizer.cpp
    src/optimization/system_tweaks.cpp
    src/monitoring/ping_monitor.cpp
    src/monitoring/packet_loss_monitor.cpp
    src/monitoring/stats_collector.cpp
    src/monitoring/jitter_calculator.cpp
    src/ui/main_window.cpp
    src/ui/game_selector.cpp
    src/ui/monitoring_panel.cpp
    src/ui/settings_dialog.cpp
    ${PLATFORM_SOURCES}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/optimization
    ${CMAKE_CURRENT_SOURCE_DIR}/src/monitoring
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Widgets Qt6::Charts Qt6::Network
    Threads::Threads
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32 iphlpapi winmm)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WINSOCK_DEPRECATED_NO_WARNINGS
    )
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
