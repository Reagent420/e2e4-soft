cmake_minimum_required(VERSION 3.20)
project(GNO VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GNO_CONSOLE "Build console-only (no Qt)" ON)

find_package(Threads REQUIRED)

if(WIN32)
    add_definitions(-DPLATFORM_WINDOWS)
    set(PLATFORM_SOURCES src/core/platform_windows.cpp src/optimization/fps_optimizer_windows.cpp)
elseif(APPLE)
    add_definitions(-DPLATFORM_MACOS)
    set(PLATFORM_SOURCES src/core/platform_macos.cpp src/optimization/fps_optimizer_macos.cpp)
else()
    add_definitions(-DPLATFORM_LINUX)
    set(PLATFORM_SOURCES src/core/platform_linux.cpp src/optimization/fps_optimizer_linux.cpp)
endif()

set(CORE_SOURCES
    src/core/route_analyzer.cpp
    src/core/multipath_engine.cpp
    src/core/packet_capture.cpp
    src/core/game_detector.cpp
    src/core/network_utils.cpp
)

set(OPT_SOURCES
    src/optimization/fps_optimizer.cpp
    src/optimization/system_tweaks.cpp
)

set(MON_SOURCES
    src/monitoring/ping_monitor.cpp
    src/monitoring/packet_loss_monitor.cpp
    src/monitoring/stats_collector.cpp
    src/monitoring/jitter_calculator.cpp
)

add_executable(${PROJECT_NAME}
    src/main_console.cpp
    ${CORE_SOURCES}
    ${OPT_SOURCES}
    ${MON_SOURCES}
    ${PLATFORM_SOURCES}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/optimization
    ${CMAKE_CURRENT_SOURCE_DIR}/src/monitoring
)

target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32 iphlpapi winmm)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX _WINSOCK_DEPRECATED_NO_WARNINGS
    )
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

if(MINGW OR (WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(${PROJECT_NAME} PRIVATE -static-libgcc -static-libstdc++ -static)
endif()
