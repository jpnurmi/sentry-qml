include_guard(GLOBAL)

include(CMakeParseArguments)

function(sentry_qml_add_integration)
    set(options NO_INSTALL)
    set(one_value_args TARGET ID CLASS_NAME METADATA)
    set(multi_value_args SOURCES LIBRARIES)
    cmake_parse_arguments(PARSE_ARGV 0 arg
        "${options}"
        "${one_value_args}"
        "${multi_value_args}"
    )

    if(NOT arg_TARGET OR NOT arg_ID OR NOT arg_METADATA OR NOT arg_SOURCES)
        message(FATAL_ERROR
            "sentry_qml_add_integration requires TARGET, ID, METADATA, and SOURCES."
        )
    endif()
    if(NOT arg_ID MATCHES "^[a-z0-9][a-z0-9.-]*$")
        message(FATAL_ERROR "Invalid Sentry QML integration ID '${arg_ID}'.")
    endif()

    file(READ "${arg_METADATA}" integration_metadata)
    string(JSON metadata_id ERROR_VARIABLE metadata_error
        GET "${integration_metadata}" Id
    )
    if(metadata_error OR NOT metadata_id STREQUAL arg_ID)
        message(FATAL_ERROR
            "Integration metadata ID '${metadata_id}' does not match '${arg_ID}'."
        )
    endif()

    if(TARGET SentryQml)
        set(core_target SentryQml)
    elseif(TARGET SentryQml::SentryQml)
        set(core_target SentryQml::SentryQml)
    else()
        message(FATAL_ERROR "The SentryQml target must exist before adding an integration.")
    endif()

    get_target_property(core_type ${core_target} TYPE)
    if(core_type STREQUAL "STATIC_LIBRARY" OR ANDROID OR EMSCRIPTEN
       OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set(plugin_type STATIC)
    else()
        set(plugin_type SHARED)
    endif()

    set(class_name_args)
    if(arg_CLASS_NAME)
        list(APPEND class_name_args CLASS_NAME "${arg_CLASS_NAME}")
    endif()

    qt_add_plugin(${arg_TARGET}
        ${plugin_type}
        ${class_name_args}
        OUTPUT_TARGETS plugin_auxiliary_targets
        ${arg_SOURCES}
    )
    target_link_libraries(${arg_TARGET}
        PRIVATE
            ${core_target}
            ${arg_LIBRARIES}
    )
    set_target_properties(${arg_TARGET} PROPERTIES
        SENTRY_QML_INTEGRATION_ID "${arg_ID}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/sentry-integrations"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/sentry-integrations"
    )

    if(NOT arg_NO_INSTALL AND NOT plugin_type STREQUAL "STATIC")
        include(GNUInstallDirs)
        install(TARGETS ${arg_TARGET}
            LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/sentry-qml/integrations"
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}/sentry-qml/integrations"
        )
    endif()

    set(${arg_TARGET}_AUXILIARY_TARGETS "${plugin_auxiliary_targets}" PARENT_SCOPE)
endfunction()

function(sentry_qml_deploy_integrations)
    set(options)
    set(one_value_args TARGET)
    set(multi_value_args INTEGRATIONS)
    cmake_parse_arguments(PARSE_ARGV 0 arg
        "${options}"
        "${one_value_args}"
        "${multi_value_args}"
    )

    if(NOT arg_TARGET OR NOT arg_INTEGRATIONS)
        message(FATAL_ERROR
            "sentry_qml_deploy_integrations requires TARGET and INTEGRATIONS."
        )
    endif()

    foreach(integration IN LISTS arg_INTEGRATIONS)
        if(NOT TARGET ${integration})
            message(FATAL_ERROR "Unknown integration target '${integration}'.")
        endif()
        get_target_property(integration_type ${integration} TYPE)
        if(integration_type STREQUAL "STATIC_LIBRARY")
            target_link_libraries(${arg_TARGET} PRIVATE ${integration})
        else()
            if(APPLE)
                set(integration_deploy_dir
                    "$<TARGET_FILE_DIR:${arg_TARGET}>/../PlugIns/sentry-integrations"
                )
            else()
                set(integration_deploy_dir
                    "$<TARGET_FILE_DIR:${arg_TARGET}>/sentry-integrations"
                )
            endif()
            add_custom_command(TARGET ${arg_TARGET} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                    "${integration_deploy_dir}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:${integration}>"
                    "${integration_deploy_dir}/$<TARGET_FILE_NAME:${integration}>"
                VERBATIM
            )
            if(WIN32)
                add_custom_command(TARGET ${arg_TARGET} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy -t
                        "${integration_deploy_dir}"
                        $<TARGET_RUNTIME_DLLS:${integration}>
                    COMMAND_EXPAND_LISTS
                    VERBATIM
                )
            endif()
        endif()
    endforeach()
endfunction()
