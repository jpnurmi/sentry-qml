if(NOT TARGET ZLIB::ZLIB)
    find_package(ZLIB QUIET)
endif()

function(add_sentry_qml_test target)
    add_executable(${target}
        ${ARGN}
    )

    target_compile_definitions(${target} PRIVATE
        SENTRY_QML_IMPORT_PATH="${SENTRY_QML_OUTPUT_DIRECTORY}"
    )

    if(SENTRY_QML_SDK STREQUAL "cocoa")
        target_compile_definitions(${target} PRIVATE
            SENTRY_QML_TEST_SDK_COCOA
        )
    endif()

    if(SENTRY_BACKEND STREQUAL "crashpad")
        target_compile_definitions(${target} PRIVATE
            SENTRY_QML_TEST_SDK_CRASHPAD
        )
    endif()

    target_link_libraries(${target}
        PRIVATE
            SentryQml
            Qt6::Core
            Qt6::Network
            Qt6::Qml
            Qt6::Test
    )

    if(TARGET ZLIB::ZLIB)
        target_link_libraries(${target} PRIVATE
            ZLIB::ZLIB
        )
        target_compile_definitions(${target} PRIVATE
            SENTRY_QML_TEST_HAS_ZLIB
        )
    endif()

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

    if(TARGET crashpad_handler)
        add_dependencies(${target} crashpad_handler)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:crashpad_handler>"
                "$<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_NAME:crashpad_handler>"
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

option(SENTRY_QML_BUILD_E2E_TESTS "Build the opt-in E2E app that sends events to a real Sentry instance" OFF)

file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/qml" SENTRY_QML_TEST_QML_DIR)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/e2e" SENTRY_QML_E2E_QML_DIR)

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

if(SENTRY_QML_BUILD_E2E_TESTS)
    add_executable(sentry_qml_e2e_app
        e2e/e2e_app.cpp
    )

    target_compile_definitions(sentry_qml_e2e_app PRIVATE
        SENTRY_QML_IMPORT_PATH="${SENTRY_QML_OUTPUT_DIRECTORY}"
        SENTRY_QML_E2E_QML_DIR="${SENTRY_QML_E2E_QML_DIR}"
    )

    target_link_libraries(sentry_qml_e2e_app
        PRIVATE
            SentryQml
            Qt6::Core
            Qt6::Network
            Qt6::Qml
    )

    add_dependencies(sentry_qml_e2e_app SentryQmlplugin)

    if(TARGET sentry-crash)
        add_dependencies(sentry_qml_e2e_app sentry-crash)
        add_custom_command(TARGET sentry_qml_e2e_app POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:sentry-crash>"
                "$<TARGET_FILE_DIR:sentry_qml_e2e_app>/$<TARGET_FILE_NAME:sentry-crash>"
            VERBATIM
        )
    endif()

    if(TARGET crashpad_handler)
        add_dependencies(sentry_qml_e2e_app crashpad_handler)
        add_custom_command(TARGET sentry_qml_e2e_app POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:crashpad_handler>"
                "$<TARGET_FILE_DIR:sentry_qml_e2e_app>/$<TARGET_FILE_NAME:crashpad_handler>"
            VERBATIM
        )
    endif()

    target_sources(sentry_qml_e2e_app PRIVATE
        e2e/E2EApp.qml
    )
endif()
