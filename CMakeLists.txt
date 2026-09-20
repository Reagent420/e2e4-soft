cmake_minimum_required(VERSION 3.20)
project(GNO VERSION 3.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)

option(GNO_CONSOLE "Build console-only (no Qt)" OFF)
option(GNO_TESTS "Build unit tests" ON)

find_package(Threads REQUIRED)

if(WIN32)
    add_definitions(-DPLATFORM_WINDOWS)
    # Легаси-слой PlatformOptimizer/FPSOptimizerPlatform удалён: он был мёртвым
    # (никто не включал его заголовки) и писал в реестр напрямую, в обход
    # transactional движка. Все правки системы идут через remediation/.
    set(PLATFORM_SOURCES src/remediation/windows_state_api.cpp)
elseif(APPLE)
    add_definitions(-DPLATFORM_MACOS)
    set(PLATFORM_SOURCES)
else()
    add_definitions(-DPLATFORM_LINUX)
    set(PLATFORM_SOURCES)
endif()

set(CORE_SOURCES
    src/core/route_analyzer.cpp
    src/core/multipath_engine.cpp
    src/core/game_detector.cpp
    src/core/network_utils.cpp
    src/core/game_profiles.cpp
    src/core/speed_test.cpp
    src/core/dns_manager.cpp
    src/core/icmp_probe.cpp
    src/core/logger.cpp
    src/core/process_monitor.cpp
    src/core/session_history.cpp
    src/core/game_watcher.cpp
    src/core/connection_grader.cpp
    src/core/pro_presets.cpp
    src/core/system_audit.cpp
    src/core/launch_diagnostics.cpp
    src/core/problem_db.cpp
    src/core/json_persistence.cpp
    src/core/capability_matrix.cpp
    src/core/plain_language.cpp
    src/core/report_text.cpp
    src/core/sha256.cpp
    src/core/simple_json.cpp
    src/core/updater.cpp
    src/diagnostics/diagnostic_types.cpp
    src/diagnostics/endpoint_observer.cpp
    src/diagnostics/network_sampler.cpp
    src/diagnostics/probe_client.cpp
    src/diagnostics/diagnostics_service.cpp
    src/remediation/backup_store.cpp
    src/remediation/windows_fix_action.cpp
    src/remediation/fix_transaction.cpp
    src/remediation/remediation_service.cpp
    src/remediation/target_discovery.cpp
    src/remediation/legacy_bridge.cpp
    src/remediation/boost_gate.cpp
    src/core/platform_netscan.cpp
    src/core/sqlite_history_store.cpp
    src/core/fps_boost.cpp
    src/core/system_manager.cpp
    src/core/tweak_service.cpp
)

set(OPT_SOURCES
    src/optimization/fps_optimizer.cpp
)

set(MON_SOURCES
    src/monitoring/ping_monitor.cpp
    src/monitoring/packet_loss_monitor.cpp
    src/monitoring/stats_collector.cpp
    src/monitoring/jitter_calculator.cpp
)

set(UI_SOURCES
    src/ui/main_window.cpp
    src/ui/sidebar.cpp
    src/ui/dashboard.cpp
    src/ui/game_list.cpp
    src/ui/game_profiles_widget.cpp
    src/ui/monitoring.cpp
    src/ui/optimizer.cpp
    src/ui/settings_page.cpp
    src/ui/system_tray.cpp
    src/ui/network_tools.cpp
    src/ui/process_monitor_widget.cpp
    src/ui/session_history_widget.cpp
    src/ui/diagnostics_widget.cpp
    src/ui/multipath_widget.cpp
    src/ui/geo_map.cpp
    src/ui/overlay.cpp
    src/ui/report_export.cpp
    src/ui/remediation_widget.cpp
    src/ui/fine_tune_widget.cpp
    src/ui/fps_boost_widget.cpp
    src/ui/knowledge_widget.cpp
    src/ui/tabbed_section.h
    src/ui/theme_editor.cpp
    src/ui/update_checker.cpp
    src/monitoring/monitoring_service.cpp
)

set(COMMON_LIBS Threads::Threads)

# Vendored SQLite (amalgamation) so console/tests build without a package manager.
add_library(gno_sqlite3 STATIC third_party/sqlite/sqlite3.c)
target_include_directories(gno_sqlite3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite)
target_compile_definitions(gno_sqlite3 PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION)
if(UNIX)
    target_link_libraries(gno_sqlite3 PUBLIC m)
endif()

if(WIN32)
    list(APPEND COMMON_LIBS ws2_32 iphlpapi winmm powrprof gno_sqlite3)
else()
    list(APPEND COMMON_LIBS gno_sqlite3)
endif()

# Console target (always built)
add_executable(${PROJECT_NAME}-console
    src/main_console.cpp
    ${CORE_SOURCES}
    ${OPT_SOURCES}
    ${MON_SOURCES}
    ${PLATFORM_SOURCES}
)

target_include_directories(${PROJECT_NAME}-console PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/optimization
    ${CMAKE_CURRENT_SOURCE_DIR}/src/monitoring
)

target_link_libraries(${PROJECT_NAME}-console PRIVATE ${COMMON_LIBS})

if(WIN32)
    target_compile_definitions(${PROJECT_NAME}-console PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX _WINSOCK_DEPRECATED_NO_WARNINGS
    )
endif()

set_target_properties(${PROJECT_NAME}-console PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

if(MINGW OR (WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(${PROJECT_NAME}-console PRIVATE -static-libgcc -static-libstdc++ -static)
endif()

# GUI target (requires Qt6)
if(NOT GNO_CONSOLE)
    find_package(Qt6 COMPONENTS Widgets Charts Network QUIET)
    if(Qt6_FOUND)
        message(STATUS "Qt6 found: ${Qt6_VERSION} - Building GUI target")

        add_executable(${PROJECT_NAME}
            WIN32
            src/main_gui.cpp
            ${CORE_SOURCES}
            ${OPT_SOURCES}
            ${MON_SOURCES}
            ${PLATFORM_SOURCES}
            ${UI_SOURCES}
        )

        target_include_directories(${PROJECT_NAME} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/src/core
            ${CMAKE_CURRENT_SOURCE_DIR}/src/optimization
            ${CMAKE_CURRENT_SOURCE_DIR}/src/monitoring
            ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
        )

        target_link_libraries(${PROJECT_NAME} PRIVATE ${COMMON_LIBS} Qt6::Widgets Qt6::Charts Qt6::Network)

        if(WIN32)
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
    else()
        message(STATUS "Qt6 not found - GUI target will not be built")
    endif()
endif()

# Unit tests target
if(GNO_TESTS)
    message(STATUS "Building unit tests with doctest")
    
    add_executable(${PROJECT_NAME}-tests
        tests/unit_tests.cpp
        ${CORE_SOURCES}
        ${OPT_SOURCES}
        ${MON_SOURCES}
        ${PLATFORM_SOURCES}
    )
    
    target_include_directories(${PROJECT_NAME}-tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/optimization
        ${CMAKE_CURRENT_SOURCE_DIR}/src/monitoring
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    )
    
    target_link_libraries(${PROJECT_NAME}-tests PRIVATE ${COMMON_LIBS})
    
    if(WIN32)
        target_compile_definitions(${PROJECT_NAME}-tests PRIVATE
            WIN32_LEAN_AND_MEAN NOMINMAX _WINSOCK_DEPRECATED_NO_WARNINGS
        )
    endif()
    
    set_target_properties(${PROJECT_NAME}-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    
    if(MINGW OR (WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
        target_link_options(${PROJECT_NAME}-tests PRIVATE -static-libgcc -static-libstdc++ -static)
    endif()
    
    add_test(NAME GNO-UnitTests COMMAND ${PROJECT_NAME}-tests)
endif()
