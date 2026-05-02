if(NOT DEFINED TEST_EXECUTABLE)
    message(FATAL_ERROR "TEST_EXECUTABLE is not set")
endif()

if(NOT DEFINED TEST_NAME)
    get_filename_component(TEST_NAME "${TEST_EXECUTABLE}" NAME_WE)
endif()

if(NOT DEFINED TEST_OUTPUT_DIR)
    get_filename_component(TEST_OUTPUT_DIR "${TEST_EXECUTABLE}" DIRECTORY)
endif()

set(output_file "${TEST_OUTPUT_DIR}/${TEST_NAME}-qtest-output.txt")
file(REMOVE "${output_file}")

execute_process(
    COMMAND "${TEST_EXECUTABLE}" -o "${output_file},txt"
    RESULT_VARIABLE test_result
    OUTPUT_VARIABLE test_stdout
    ERROR_VARIABLE test_stderr
)

if(test_stdout)
    message("${test_stdout}")
endif()

if(test_stderr)
    message("${test_stderr}")
endif()

if(EXISTS "${output_file}")
    file(READ "${output_file}" qtest_output)
    if(qtest_output)
        message("${qtest_output}")
    endif()
endif()

if(NOT test_result STREQUAL "0")
    message(FATAL_ERROR "Test failed with exit code ${test_result}")
endif()
