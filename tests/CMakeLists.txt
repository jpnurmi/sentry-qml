if(NOT TARGET ZLIB::ZLIB)
    find_package(ZLIB QUIET)
endif()

if(APPLE AND TARGET ZLIB::ZLIB AND ZLIB_INCLUDE_DIR AND CMAKE_OSX_SYSROOT)
    get_filename_component(sentry_qml_zlib_include_dir "${ZLIB_INCLUDE_DIR}" REALPATH)
    get_filename_component(sentry_qml_apple_sdk_include_dir "${CMAKE_OSX_SYSROOT}/usr/include" REALPATH)
    if(sentry_qml_zlib_include_dir STREQUAL sentry_qml_apple_sdk_include_dir)
        # Apple SDK headers are already found through -isysroot. Adding the SDK
        # C header directory explicitly can put it before libc++ wrapper headers.
        set_target_properties(ZLIB::ZLIB PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "")
    endif()
endif()

function(add_sentry_qml_android_test target)
    get_target_property(target_binary_dir ${target} BINARY_DIR)
    if(QT_USE_TARGET_ANDROID_BUILD_DIR)
        set(android_build_dir "${target_binary_dir}/android-build-${target}")
    else()
        set(android_build_dir "${target_binary_dir}/android-build")
    endif()

    set(android_test_runner "${QT_HOST_PATH}/${QT6_HOST_INFO_BINDIR}/androidtestrunner")
    set(android_make_command "${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ${target}_make_apk")
    set(android_test_arguments
        --path "${android_build_dir}"
        --adb "${ANDROID_SDK_ROOT}/platform-tools/adb"
        --skip-install-root
        --make "${android_make_command}"
        --apk "${android_build_dir}/${target}.apk"
        --timeout 240
        --verbose
    )
    if(ANDROID_NDK_ROOT)
        list(APPEND android_test_arguments
            --ndk-stack "${ANDROID_NDK_ROOT}/ndk-stack"
        )
    endif()

    add_test(NAME ${target}
        COMMAND
            "${android_test_runner}"
            ${android_test_arguments}
            --
            -txt
    )
    set_tests_properties(${target} PROPERTIES
        RUN_SERIAL TRUE
        TIMEOUT 300
        TIMEOUT_SIGNAL_NAME "SIGINT"
        TIMEOUT_SIGNAL_GRACE_PERIOD 10.0
    )
endfunction()

function(add_sentry_qml_test target)
    if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
        qt_add_executable(${target}
            ${ARGN}
        )
    else()
        add_executable(${target}
            ${ARGN}
        )
    endif()

    target_compile_definitions(${target} PRIVATE
        SENTRY_QML_IMPORT_PATH="${SENTRY_QML_OUTPUT_DIRECTORY}"
    )

    if(SENTRY_QML_SDK STREQUAL "cocoa" OR SENTRY_QML_SDK STREQUAL "ios")
        target_compile_definitions(${target} PRIVATE
            SENTRY_QML_TEST_SDK_COCOA
        )
    endif()

    if(SENTRY_BACKEND STREQUAL "crashpad")
        target_compile_definitions(${target} PRIVATE
            SENTRY_QML_TEST_SDK_CRASHPAD
        )
    endif()

    if(EMSCRIPTEN)
        target_compile_definitions(${target} PRIVATE
            SENTRY_QML_TEST_SDK_WASM
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

    if(EMSCRIPTEN)
        foreach(plugin_target IN ITEMS
            Qt6::qmlplugin
            Qt6::qtqmlcoreplugin
            Qt6::modelsplugin
            Qt6::qtquick2plugin
            SentryQmlplugin
        )
            if(TARGET ${plugin_target})
                target_link_libraries(${target} PRIVATE ${plugin_target})
            endif()
        endforeach()
    endif()

    if(ANDROID)
        target_link_libraries(${target} PRIVATE Qt6::Gui)
    endif()

    if(TARGET ZLIB::ZLIB)
        target_link_libraries(${target} PRIVATE
            ZLIB::ZLIB
        )
        target_compile_definitions(${target} PRIVATE
            SENTRY_QML_TEST_HAS_ZLIB
        )
    endif()

    add_dependencies(${target} SentryQmlplugin)

    if(ANDROID AND COMMAND sentry_qml_configure_android_target)
        target_compile_definitions(${target} PRIVATE
            SENTRY_QML_TEST_SDK_ANDROID
        )
        set_target_properties(${target} PROPERTIES
            QT_ANDROID_PACKAGE_NAME "io.sentry.qml.${target}"
            QT_ANDROID_TARGET_SDK_VERSION 27
        )
        sentry_qml_configure_android_target(${target})
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "iOS" AND COMMAND sentry_qml_configure_ios_target)
        string(REPLACE "_" "-" ios_bundle_name "${target}")
        set_target_properties(${target} PROPERTIES
            MACOSX_BUNDLE_GUI_IDENTIFIER "io.sentry.qml.${ios_bundle_name}"
        )
        sentry_qml_configure_ios_target(${target})
    endif()

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

    if(ANDROID)
        add_sentry_qml_android_test(${target})
    elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        add_test(NAME ${target}
            COMMAND
                "${CMAKE_CURRENT_SOURCE_DIR}/ios/run_simulator_test.sh"
                "$<TARGET_BUNDLE_DIR:${target}>"
                -txt
        )
        set_tests_properties(${target} PROPERTIES
            RUN_SERIAL TRUE
            TIMEOUT 300
        )
    elseif(SENTRY_QML_TEST_JUNIT)
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

target_link_libraries(tst_integration PRIVATE Qt6::Quick)

set_source_files_properties(qml/IntegrationTest.qml PROPERTIES
    QT_RESOURCE_ALIAS IntegrationTest.qml
)
set_source_files_properties(qml/ViewHierarchyTest.qml PROPERTIES
    QT_RESOURCE_ALIAS ViewHierarchyTest.qml
)
set_source_files_properties(qml/ScreenshotTest.qml PROPERTIES
    QT_RESOURCE_ALIAS ScreenshotTest.qml
)
qt_add_resources(tst_integration sentry_qml_integration_test_qml
    PREFIX "/sentry-qml-tests"
    FILES
        qml/IntegrationTest.qml
        qml/ScreenshotTest.qml
        qml/ViewHierarchyTest.qml
)

if(NOT ANDROID)
    set_tests_properties(tst_integration PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endif()

if(TARGET sentry-crash AND NOT ANDROID)
    add_sentry_qml_test(tst_crash
        tst_crash.cpp
    )

    target_compile_definitions(tst_crash PRIVATE
        SENTRY_QML_TEST_QML_DIR="${SENTRY_QML_TEST_QML_DIR}"
    )

    target_link_libraries(tst_crash PRIVATE Qt6::Gui Qt6::Quick)

    target_sources(tst_crash PRIVATE
        qml/CrashTest.qml
    )
endif()

if(SENTRY_QML_BUILD_E2E_TESTS)
    qt_add_executable(sentry_qml_e2e_app
        e2e/e2e_app.cpp
    )

    target_compile_definitions(sentry_qml_e2e_app PRIVATE
        SENTRY_QML_IMPORT_PATH="${SENTRY_QML_OUTPUT_DIRECTORY}"
    )

    target_link_libraries(sentry_qml_e2e_app
        PRIVATE
            SentryQml
            Qt6::Core
            Qt6::Gui
            Qt6::Network
            Qt6::Qml
            Qt6::Quick
    )

    add_dependencies(sentry_qml_e2e_app SentryQmlplugin)

    if(ANDROID AND COMMAND sentry_qml_configure_android_target)
        set_target_properties(sentry_qml_e2e_app PROPERTIES
            QT_ANDROID_PACKAGE_NAME "io.sentry.qml.e2e"
            QT_ANDROID_TARGET_SDK_VERSION 27
        )
        sentry_qml_configure_android_target(sentry_qml_e2e_app)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "iOS" AND COMMAND sentry_qml_configure_ios_target)
        set_target_properties(sentry_qml_e2e_app PROPERTIES
            MACOSX_BUNDLE_GUI_IDENTIFIER "io.sentry.qml.e2e"
        )
        sentry_qml_configure_ios_target(sentry_qml_e2e_app)
    endif()

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

    set_source_files_properties(e2e/E2EApp.qml PROPERTIES
        QT_RESOURCE_ALIAS E2EApp.qml
    )
    qt_add_resources(sentry_qml_e2e_app sentry_qml_e2e_qml
        PREFIX "/sentry-qml-e2e"
        FILES
            e2e/E2EApp.qml
    )
endif()
