if(NOT DEFINED ARTIFACT)
    if(NOT DEFINED SOURCE_DIR)
        message(FATAL_ERROR "SOURCE_DIR is required for the source gate")
    endif()
    execute_process(
        COMMAND git -C "${SOURCE_DIR}" ls-files "*.exe" "*.dll" "*.zip"
        RESULT_VARIABLE tracked_result
        OUTPUT_VARIABLE tracked_artifacts
        ERROR_VARIABLE tracked_error
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT tracked_result EQUAL 0)
        message(FATAL_ERROR "cannot inspect tracked release artifacts: ${tracked_error}")
    endif()
    if(NOT tracked_artifacts STREQUAL "")
        message(FATAL_ERROR "tracked release artifacts are prohibited:\n${tracked_artifacts}")
    endif()
    return()
endif()

if(NOT EXISTS "${ARTIFACT}")
    message(FATAL_ERROR "release artifact does not exist: ${ARTIFACT}")
endif()

file(STRINGS "${ARTIFACT}" prohibited_symbols
     REGEX "FPSOptimizer|SystemTweaks|MultipathEngine|PlatformOptimizer|applyNetworkTweak|applyRegistryTweak|optimizePowerPlan|optimizeVirtualMemory")
if(prohibited_symbols)
    message(FATAL_ERROR "release artifact contains prohibited mutator symbols: ${prohibited_symbols}")
endif()
