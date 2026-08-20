if(NOT DEFINED EXECUTABLE OR NOT DEFINED ARGUMENT OR NOT DEFINED EXPECTED_EXIT OR NOT DEFINED EXPECTED_TEXT)
    message(FATAL_ERROR "EXECUTABLE, ARGUMENT, EXPECTED_EXIT, and EXPECTED_TEXT are required")
endif()

execute_process(
    COMMAND "${EXECUTABLE}" "${ARGUMENT}"
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
