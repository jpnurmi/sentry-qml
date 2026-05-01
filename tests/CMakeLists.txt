qt_add_executable(tst_sentryqml
    tst_sentryqml.cpp
)

target_compile_definitions(tst_sentryqml PRIVATE
    SENTRY_QML_IMPORT_PATH="${SENTRY_QML_OUTPUT_DIRECTORY}"
)

if(TARGET Qt6::QmlPrivate)
    target_compile_definitions(tst_sentryqml PRIVATE
        SENTRY_QML_HAS_QML_PRIVATE
    )
endif()

target_link_libraries(tst_sentryqml
    PRIVATE
        SentryQml
        Qt6::Core
        Qt6::Network
        Qt6::Qml
        Qt6::Test
)

add_dependencies(tst_sentryqml SentryQmlplugin)

if(TARGET sentry-crash)
    add_dependencies(tst_sentryqml sentry-crash)
    add_custom_command(TARGET tst_sentryqml POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:sentry-crash>"
            "$<TARGET_FILE_DIR:tst_sentryqml>/$<TARGET_FILE_NAME:sentry-crash>"
        VERBATIM
    )
endif()

add_test(NAME tst_sentryqml COMMAND tst_sentryqml)
