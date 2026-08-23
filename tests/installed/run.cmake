if(NOT DEFINED SOURCE_DIR OR NOT DEFINED BINARY_DIR OR NOT DEFINED QT_DIR)
    message(FATAL_ERROR "SOURCE_DIR, BINARY_DIR, and QT_DIR are required.")
endif()

set(install_dir "${BINARY_DIR}/installed consumer-å-prefix")
set(consumer_dir "${BINARY_DIR}/installed consumer-å-build")
file(REMOVE_RECURSE "${install_dir}" "${consumer_dir}")

set(install_config_args)
set(consumer_configure_args)
set(consumer_build_config_args)
if(DEFINED CONFIG AND NOT CONFIG STREQUAL "")
    list(APPEND install_config_args --config "${CONFIG}")
    list(APPEND consumer_configure_args "-DCMAKE_BUILD_TYPE=${CONFIG}")
    list(APPEND consumer_build_config_args --config "${CONFIG}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${BINARY_DIR}"
        --prefix "${install_dir}"
        ${install_config_args}
    RESULT_VARIABLE result
)
if(result)
    message(FATAL_ERROR "Installing Sentry QML failed with code ${result}.")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -S "${SOURCE_DIR}/tests/installed"
        -B "${consumer_dir}"
        "-DQt6_DIR=${QT_DIR}"
        "-DSentryQml_DIR=${install_dir}/lib/cmake/SentryQml"
        "-Dsentry_DIR=${install_dir}/lib/cmake/sentry"
        ${consumer_configure_args}
    RESULT_VARIABLE result
)
if(result)
    message(FATAL_ERROR "Configuring the installed consumer failed with code ${result}.")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${consumer_dir}"
        ${consumer_build_config_args}
    RESULT_VARIABLE result
)
if(result)
    message(FATAL_ERROR "Building the installed consumer failed with code ${result}.")
endif()

if(WIN32)
    set(executable_name "sentry_qml_installed_consumer.exe")
else()
    set(executable_name "sentry_qml_installed_consumer")
endif()
set(executable "${consumer_dir}/${CONFIG}/${executable_name}")
if(NOT EXISTS "${executable}")
    set(executable "${consumer_dir}/${executable_name}")
endif()
if(NOT EXISTS "${executable}")
    message(FATAL_ERROR "The installed consumer executable was not found for configuration '${CONFIG}'.")
endif()

if(WIN32)
    set(handler_name "crashpad_handler.exe")
else()
    set(handler_name "crashpad_handler")
endif()
set(handler "${install_dir}/bin/${handler_name}")
if(EXISTS "${handler}")
    get_filename_component(executable_dir "${executable}" DIRECTORY)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${handler}"
            "${executable_dir}/${handler_name}"
        RESULT_VARIABLE result
    )
    if(result)
        message(FATAL_ERROR "Deploying the installed crash handler failed with code ${result}.")
    endif()
endif()

get_filename_component(qt_prefix "${QT_DIR}/../../.." ABSOLUTE)
if(WIN32)
    set(runtime_path "PATH=${qt_prefix}/bin;$ENV{PATH}")
elseif(APPLE)
    set(runtime_path "DYLD_LIBRARY_PATH=${qt_prefix}/lib:$ENV{DYLD_LIBRARY_PATH}")
else()
    set(runtime_path "LD_LIBRARY_PATH=${qt_prefix}/lib:$ENV{LD_LIBRARY_PATH}")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "${runtime_path}" "${executable}"
    RESULT_VARIABLE result
)
if(result)
    message(FATAL_ERROR "The installed consumer failed with code ${result}.")
endif()
