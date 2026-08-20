if(NOT DEFINED EXECUTABLE OR NOT DEFINED EXPECTED_EXIT OR NOT DEFINED EXPECTED_TEXT OR
   (NOT DEFINED ARGUMENT AND NOT DEFINED ARGUMENTS))
    message(FATAL_ERROR "EXECUTABLE, ARGUMENT(S), EXPECTED_EXIT, and EXPECTED_TEXT are required")
endif()

execute_process(
    COMMAND "${EXECUTABLE}" ${ARGUMENT} ${ARGUMENTS}
    RESULT_VARIABLE actual_exit
    OUTPUT_VARIABLE actual_output
    ERROR_VARIABLE actual_error)

if(NOT actual_exit EQUAL EXPECTED_EXIT)
    message(FATAL_ERROR "Expected exit ${EXPECTED_EXIT}, got ${actual_exit}")
endif()

set(actual_text "${actual_output}${actual_error}")
string(FIND "${actual_text}" "${EXPECTED_TEXT}" expected_text_position)
if(expected_text_position EQUAL -1)
    message(FATAL_ERROR "Expected output to contain '${EXPECTED_TEXT}', got '${actual_text}'")
endif()

if(DEFINED FORBIDDEN_TEXT)
    string(FIND "${actual_text}" "${FORBIDDEN_TEXT}" forbidden_text_position)
    if(NOT forbidden_text_position EQUAL -1)
        message(FATAL_ERROR "Output must not contain '${FORBIDDEN_TEXT}', got '${actual_text}'")
    endif()
endif()
