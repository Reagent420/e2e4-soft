cmake_minimum_required(VERSION 3.24)
project(GNO VERSION 1.2.0 LANGUAGES CXX)

include(CTest)
include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)

option(GNO_CONSOLE "Build console-only (no Qt)" OFF)
option(GNO_TESTS "Build unit tests" ON)
option(GNO_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)

if(GNO_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

find_package(Threads REQUIRED)

if(WIN32)
    add_definitions(-DPLATFORM_WINDOWS)
elseif(APPLE)
    add_definitions(-DPLATFORM_MACOS)
else()
    add_definitions(-DPLATFORM_LINUX)
endif()

function(gno_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

function(gno_add_release_artifact_gate target)
    add_test(
        NAME GNO-ReleaseArtifact-${target}
        COMMAND ${CMAKE_COMMAND}
            "-DARTIFACT=$<TARGET_FILE:${target}>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/release_gate.cmake")
endfunction()

set(CORE_SOURCES
    src/core/route_analyzer.cpp
    src/core/game_detector.cpp
    src/core/network_utils.cpp
    src/core/game_profiles.cpp
    src/core/speed_test.cpp
    src/core/dns_manager.cpp
    src/core/process_monitor.cpp
    src/core/session_history.cpp
    src/core/game_watcher.cpp
    src/core/connection_grader.cpp
    src/core/input_validation.cpp
    src/core/json_persistence.cpp
    src/diagnostics/diagnostic_types.cpp
    src/remediation/fix_transaction.cpp
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
    src/ui/monitoring.cpp
    src/ui/settings_page.cpp
    src/ui/system_tray.cpp
    src/ui/network_tools.cpp
    src/ui/session_history_widget.cpp
)

set(COMMON_LIBS Threads::Threads nlohmann_json::nlohmann_json)

if(WIN32)
    list(APPEND COMMON_LIBS ws2_32 iphlpapi winmm)
endif()

# Console target (always built)
add_executable(${PROJECT_NAME}-console
    src/main_console.cpp
    ${CORE_SOURCES}
    ${MON_SOURCES}
)

gno_enable_warnings(${PROJECT_NAME}-console)

target_include_directories(${PROJECT_NAME}-console PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/monitoring
    ${CMAKE_CURRENT_SOURCE_DIR}/src/diagnostics
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

if(GNO_TESTS)
    add_test(
        NAME GNO-ReleaseSourceGate
        COMMAND ${CMAKE_COMMAND}
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/release_gate.cmake")
    gno_add_release_artifact_gate(${PROJECT_NAME}-console)
endif()

# GUI target (requires Qt6)
if(NOT GNO_CONSOLE)
    find_package(Qt6 COMPONENTS Widgets Charts QUIET)
    if(Qt6_FOUND)
        message(STATUS "Qt6 found: ${Qt6_VERSION} - Building GUI target")

        add_executable(${PROJECT_NAME}
            WIN32
            src/main_gui.cpp
            ${CORE_SOURCES}
            ${MON_SOURCES}
            ${UI_SOURCES}
        )

        gno_enable_warnings(${PROJECT_NAME})
        set_target_properties(${PROJECT_NAME} PROPERTIES AUTOMOC ON)

        target_include_directories(${PROJECT_NAME} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/src/core
            ${CMAKE_CURRENT_SOURCE_DIR}/src/monitoring
            ${CMAKE_CURRENT_SOURCE_DIR}/src/diagnostics
            ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
        )

        target_link_libraries(${PROJECT_NAME} PRIVATE ${COMMON_LIBS} Qt6::Widgets Qt6::Charts)

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

        if(GNO_TESTS)
            add_executable(${PROJECT_NAME}-ui-tests
                tests/ui_tests.cpp
                ${CORE_SOURCES}
                ${MON_SOURCES}
                ${UI_SOURCES})
            target_include_directories(${PROJECT_NAME}-ui-tests PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/src
                ${CMAKE_CURRENT_SOURCE_DIR}/src/core
                ${CMAKE_CURRENT_SOURCE_DIR}/src/monitoring
                ${CMAKE_CURRENT_SOURCE_DIR}/src/diagnostics
                ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
                ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
            target_link_libraries(${PROJECT_NAME}-ui-tests PRIVATE ${COMMON_LIBS} Qt6::Widgets Qt6::Charts)
            gno_enable_warnings(${PROJECT_NAME}-ui-tests)
            set_target_properties(${PROJECT_NAME}-ui-tests PROPERTIES AUTOMOC ON)
            gno_add_release_artifact_gate(${PROJECT_NAME})
            add_test(NAME GNO-UITests COMMAND ${PROJECT_NAME}-ui-tests)
            set_tests_properties(GNO-UITests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
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
        tests/foundation_tests.cpp
        tests/remediation_tests.cpp
        ${CORE_SOURCES}
        ${MON_SOURCES}
    )

    gno_enable_warnings(${PROJECT_NAME}-tests)
    
    target_include_directories(${PROJECT_NAME}-tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/monitoring
        ${CMAKE_CURRENT_SOURCE_DIR}/src/diagnostics
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
    add_test(NAME GNO-GameWatcherLifecycle
        COMMAND ${PROJECT_NAME}-tests --test-case="GameWatcher stop interrupts a confirmed long wait")
    set_tests_properties(GNO-GameWatcherLifecycle PROPERTIES TIMEOUT 5)
    add_test(NAME GNO-PingMonitorLifecycle
        COMMAND ${PROJECT_NAME}-tests --test-case="PingMonitor reaps a worker stopped by its callback before restart")
    set_tests_properties(GNO-PingMonitorLifecycle PROPERTIES TIMEOUT 5)
    add_test(NAME GNO-GameWatcherConcurrentStop
        COMMAND ${PROJECT_NAME}-tests --test-case="GameWatcher external and callback stops do not deadlock")
    set_tests_properties(GNO-GameWatcherConcurrentStop PROPERTIES TIMEOUT 5)
    add_test(NAME GNO-PingMonitorConcurrentStop
        COMMAND ${PROJECT_NAME}-tests --test-case="PingMonitor external and callback stops do not deadlock")
    set_tests_properties(GNO-PingMonitorConcurrentStop PROPERTIES TIMEOUT 5)
    add_test(NAME GNO-GameWatcherGeneration
        COMMAND ${PROJECT_NAME}-tests --test-case="GameWatcher rejects a start while its previous generation is stopping")
    set_tests_properties(GNO-GameWatcherGeneration PROPERTIES TIMEOUT 5)
    add_test(NAME GNO-PingMonitorGeneration
        COMMAND ${PROJECT_NAME}-tests --test-case="PingMonitor rejects a start while its previous generation is stopping")
    set_tests_properties(GNO-PingMonitorGeneration PROPERTIES TIMEOUT 5)

    add_test(NAME GNO-RejectBoost
        COMMAND ${CMAKE_COMMAND}
            -DEXECUTABLE=$<TARGET_FILE:${PROJECT_NAME}-console>
            -DARGUMENT=--boost
            -DEXPECTED_EXIT=1
            -DEXPECTED_TEXT=Unknown\ option:\ --boost
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/assert_cli_rejection.cmake)
    add_test(NAME GNO-RejectMalformedTarget
        COMMAND ${CMAKE_COMMAND}
            "-DEXECUTABLE=$<TARGET_FILE:${PROJECT_NAME}-console>"
            "-DARGUMENTS=--target;invalid-address"
            -DEXPECTED_EXIT=2
            "-DEXPECTED_TEXT=Invalid IPv4 address: invalid-address"
            "-DFORBIDDEN_TEXT=Sent:"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/assert_cli_rejection.cmake)
endif()
