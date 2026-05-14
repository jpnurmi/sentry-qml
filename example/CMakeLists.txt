list(APPEND QML_IMPORT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/styles")
list(REMOVE_DUPLICATES QML_IMPORT_PATH)
set(QML_IMPORT_PATH "${QML_IMPORT_PATH}" CACHE STRING
    "QML import paths for Qt Creator and other QML tooling"
    FORCE
)

qt_add_executable(sentry_qml_example
    main.cpp
)

set_source_files_properties(ExampleState.qml
    PROPERTIES
        QT_QML_SINGLETON_TYPE TRUE
)

qt_add_qml_module(sentry_qml_example
    URI SentryExample
    VERSION 1.0
    QML_FILES
        controls/ActionButton.qml
        controls/BackButton.qml
        controls/Banner.qml
        controls/ComboChevron.qml
        controls/EnvelopeIcon.qml
        controls/FloatingActionButton.qml
        controls/IconToolButton.qml
        controls/LabeledDoubleSpinBox.qml
        controls/LabeledSpinBox.qml
        controls/LabeledTextField.qml
        ExampleState.qml
        Main.qml
    RESOURCES
        sentry-glyph.svg
)

qt_add_resources(sentry_qml_example sentry_qml_example_violet_style
    PREFIX
        "/"
    BASE
        "styles"
    FILES
        styles/Violet/ApplicationWindow.qml
        styles/Violet/Button.qml
        styles/Violet/CheckBox.qml
        styles/Violet/ComboBox.qml
        styles/Violet/DoubleSpinBox.qml
        styles/Violet/ItemDelegate.qml
        styles/Violet/Label.qml
        styles/Violet/Popup.qml
        styles/Violet/ScrollBar.qml
        styles/Violet/ScrollView.qml
        styles/Violet/SpinBox.qml
        styles/Violet/StackView.qml
        styles/Violet/TabButton.qml
        styles/Violet/TextArea.qml
        styles/Violet/TextField.qml
        styles/Violet/Theme.qml
        styles/Violet/ToolButton.qml
        styles/Violet/ToolTip.qml
        styles/Violet/qmldir
)

target_compile_definitions(sentry_qml_example PRIVATE
    SENTRY_QML_IMPORT_PATH="${SENTRY_QML_OUTPUT_DIRECTORY}"
)

target_link_libraries(sentry_qml_example
    PRIVATE
        SentryQml
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

if(ANDROID AND COMMAND sentry_qml_configure_android_target)
    sentry_qml_configure_android_target(sentry_qml_example)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "iOS" AND COMMAND sentry_qml_configure_ios_target)
    sentry_qml_configure_ios_target(sentry_qml_example)
endif()
