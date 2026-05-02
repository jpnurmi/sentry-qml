function(add_sentry_qml_test target)
    add_executable(${target}
        ${ARGN}
    )

    target_compile_definitions(${target} PRIVATE
        SENTRY_QML_IMPORT_PATH="${SENTRY_QML_OUTPUT_DIRECTORY}"
    )

    target_link_libraries(${target}
        PRIVATE
            SentryQml
            Qt6::Core
            Qt6::Network
            Qt6::Qml
            Qt6::Test
    )

    add_dependencies(${target} SentryQmlplugin)

    if(TARGET sentry-crash)
        add_dependencies(${target} sentry-crash)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:sentry-crash>"
                "$<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_NAME:sentry-crash>"
            VERBATIM
        )
    endif()

    if(SENTRY_QML_TEST_JUNIT)
        add_test(NAME ${target}
            COMMAND ${target}
                -o "${CMAKE_CURRENT_BINARY_DIR}/test-results/${target}.xml,junitxml"
                -o -,txt
        )
    else()
        add_test(NAME ${target} COMMAND ${target} -txt)
    endif()
endfunction()

option(SENTRY_QML_TEST_JUNIT "Write QtTest JUnit XML test reports" OFF)
if(SENTRY_QML_TEST_JUNIT)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-results")
endif()

file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/qml" SENTRY_QML_TEST_QML_DIR)

add_sentry_qml_test(tst_unit
    tst_unit.cpp
)

if(TARGET Qt6::QmlPrivate)
    target_compile_definitions(tst_unit PRIVATE
        SENTRY_QML_HAS_QML_PRIVATE
    )
endif()

add_sentry_qml_test(tst_integration
    tst_integration.cpp
)

target_compile_definitions(tst_integration PRIVATE
    SENTRY_QML_TEST_QML_DIR="${SENTRY_QML_TEST_QML_DIR}"
)

target_sources(tst_integration PRIVATE
    qml/IntegrationTest.qml
)

if(TARGET sentry-crash)
    add_sentry_qml_test(tst_crash
        tst_crash.cpp
    )

    target_compile_definitions(tst_crash PRIVATE
        SENTRY_QML_TEST_QML_DIR="${SENTRY_QML_TEST_QML_DIR}"
    )

    target_sources(tst_crash PRIVATE
        qml/CrashTest.qml
    )
endif()
