if(NOT ANDROID)
    message(FATAL_ERROR "SENTRY_QML_SDK=android is only supported when building for Android.")
endif()

set(QT_USE_TARGET_ANDROID_BUILD_DIR ON CACHE BOOL
    "Use target-specific Qt Android build directories"
)
set(QT_ANDROID_DEPLOYMENT_TYPE Debug CACHE STRING
    "Qt Android deployment type"
)

set(SENTRY_ANDROID_VERSION "8.41.0" CACHE STRING "sentry-android Gradle dependency version")
set(SENTRY_ANDROID_GRADLE_COORDINATE "io.sentry:sentry-android:${SENTRY_ANDROID_VERSION}" CACHE STRING
    "Gradle coordinate used for the Android Sentry SDK"
)

set(SENTRY_SDK_NAME "sentry.android.qml")
list(APPEND SENTRY_QML_SDK_SOURCES
    "${PROJECT_SOURCE_DIR}/src/android/sentryandroidsdk.cpp"
)

function(_sentry_qml_android_templates_dir out_var)
    _qt_internal_android_template_dir(qt_android_template_dir)
    get_filename_component(qt_android_template_parent "${qt_android_template_dir}" DIRECTORY)
    set(${out_var} "${qt_android_template_parent}/templates" PARENT_SCOPE)
endfunction()

function(_sentry_qml_configure_android_build_gradle output_file)
    _sentry_qml_android_templates_dir(qt_android_templates_dir)
    set(qt_build_gradle_template "${qt_android_templates_dir}/build.gradle")
    if(NOT EXISTS "${qt_build_gradle_template}")
        message(FATAL_ERROR "Could not find Qt Android Gradle template: ${qt_build_gradle_template}")
    endif()

    file(READ "${qt_build_gradle_template}" build_gradle)
    set(gradle_dependency_marker
        "    implementation fileTree(dir: 'libs', include: ['*.jar', '*.aar'])"
    )
    set(gradle_dependency_replacement
        "${gradle_dependency_marker}\n    //noinspection GradleDependency\n    implementation '${SENTRY_ANDROID_GRADLE_COORDINATE}'"
    )
    string(REPLACE "${gradle_dependency_marker}" "${gradle_dependency_replacement}"
        build_gradle "${build_gradle}"
    )
    string(FIND "${build_gradle}" "${SENTRY_ANDROID_GRADLE_COORDINATE}" sentry_dependency_index)
    if(sentry_dependency_index EQUAL -1)
        message(FATAL_ERROR
            "Could not add '${SENTRY_ANDROID_GRADLE_COORDINATE}' to ${qt_build_gradle_template}"
        )
    endif()

    file(WRITE "${output_file}" "${build_gradle}")
endfunction()

function(_sentry_qml_configure_android_manifest output_file)
    _sentry_qml_android_templates_dir(qt_android_templates_dir)
    set(qt_manifest_template "${qt_android_templates_dir}/AndroidManifest.xml")
    if(NOT EXISTS "${qt_manifest_template}")
        message(FATAL_ERROR "Could not find Qt Android manifest template: ${qt_manifest_template}")
    endif()

    file(READ "${qt_manifest_template}" manifest)
    set(auto_init_marker "        android:fullBackupOnly=\"false\">")
    set(auto_init_replacement
        "${auto_init_marker}\n        <meta-data\n            android:name=\"io.sentry.auto-init\"\n            android:value=\"false\" />"
    )
    string(REPLACE "${auto_init_marker}" "${auto_init_replacement}" manifest "${manifest}")
    string(FIND "${manifest}" "io.sentry.auto-init" auto_init_index)
    if(auto_init_index EQUAL -1)
        message(FATAL_ERROR
            "Could not disable sentry-android auto-init in ${qt_manifest_template}"
        )
    endif()

    file(WRITE "${output_file}" "${manifest}")
endfunction()

function(sentry_qml_configure_android_target target)
    if(NOT ANDROID)
        return()
    endif()

    if(NOT TARGET ${target})
        message(FATAL_ERROR "sentry_qml_configure_android_target expected an existing target: ${target}")
    endif()

    get_target_property(existing_package_source_dir ${target} QT_ANDROID_PACKAGE_SOURCE_DIR)
    if(existing_package_source_dir)
        message(FATAL_ERROR
            "sentry_qml_configure_android_target(${target}) needs QT_ANDROID_PACKAGE_SOURCE_DIR "
            "to add sentry-android Java sources, Gradle dependencies, and Android manifest metadata, "
            "but the target already sets it. Copy ${PROJECT_SOURCE_DIR}/src/android/java into "
            "the target package source directory, add '${SENTRY_ANDROID_GRADLE_COORDINATE}' to "
            "build.gradle, and add '<meta-data android:name=\"io.sentry.auto-init\" "
            "android:value=\"false\" />' to the application manifest."
        )
    endif()

    set(package_source_dir "${CMAKE_BINARY_DIR}/sentry-qml-android/${target}/package-source")

    if(Qt6_VERSION VERSION_GREATER_EQUAL 6.10)
        set(package_java_dir "${package_source_dir}/src/io/sentry/qml")
        file(MAKE_DIRECTORY "${package_source_dir}" "${package_java_dir}")
        _sentry_qml_configure_android_manifest(
            "${package_source_dir}/AndroidManifest.xml"
        )
        configure_file(
            "${PROJECT_SOURCE_DIR}/src/android/java/io/sentry/qml/SentryQmlBridge.java"
            "${package_java_dir}/SentryQmlBridge.java"
            COPYONLY
        )
        _sentry_qml_configure_android_build_gradle(
            "${package_source_dir}/build.gradle"
        )
        set_property(TARGET ${target} PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR "${package_source_dir}")
        return()
    endif()

    set(package_java_dir "${package_source_dir}/src/io/sentry/qml")
    file(MAKE_DIRECTORY "${package_java_dir}")
    configure_file(
        "${PROJECT_SOURCE_DIR}/src/android/package/qt6_8/AndroidManifest.xml"
        "${package_source_dir}/AndroidManifest.xml"
        COPYONLY
    )
    configure_file(
        "${PROJECT_SOURCE_DIR}/src/android/java/io/sentry/qml/SentryQmlBridge.java"
        "${package_java_dir}/SentryQmlBridge.java"
        COPYONLY
    )
    configure_file(
        "${PROJECT_SOURCE_DIR}/src/android/package/qt6_8/build.gradle.in"
        "${package_source_dir}/build.gradle"
        @ONLY
    )

    set_property(TARGET ${target} PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR "${package_source_dir}")
endfunction()
