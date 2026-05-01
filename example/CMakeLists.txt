qt_add_executable(sentry_qml_example
    main.cpp
)

qt_add_qml_module(sentry_qml_example
    URI SentryExample
    VERSION 1.0
    QML_FILES
        Main.qml
)

target_compile_definitions(sentry_qml_example PRIVATE
    SENTRY_QML_IMPORT_PATH="${SENTRY_QML_OUTPUT_DIRECTORY}"
)

target_link_libraries(sentry_qml_example
    PRIVATE
        Qt6::Core
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
)

add_dependencies(sentry_qml_example SentryQmlplugin)

if(TARGET sentry-crash)
    add_custom_target(sentry_qml_example_deploy_sentry_crash
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:sentry-crash>"
            "$<TARGET_FILE_DIR:sentry_qml_example>/$<TARGET_FILE_NAME:sentry-crash>"
        DEPENDS sentry-crash
        VERBATIM
    )
    add_dependencies(sentry_qml_example sentry_qml_example_deploy_sentry_crash)
endif()
