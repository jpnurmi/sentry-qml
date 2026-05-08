if(NOT ANDROID)
    message(FATAL_ERROR "SENTRY_QML_SDK=android is only supported when building for Android.")
endif()

set(SENTRY_ANDROID_VERSION "8.41.0" CACHE STRING "sentry-android Gradle dependency version")
set(SENTRY_ANDROID_GRADLE_COORDINATE "io.sentry:sentry-android:${SENTRY_ANDROID_VERSION}" CACHE STRING
    "Gradle coordinate used for the Android Sentry SDK"
)

set(SENTRY_SDK_NAME "sentry.android.qml")
list(APPEND SENTRY_QML_SDK_SOURCES
    "${PROJECT_SOURCE_DIR}/src/android/sentryandroidsdk.cpp"
)

function(sentry_qml_configure_android_target target)
    if(NOT ANDROID)
        return()
    endif()

    if(NOT TARGET ${target})
        message(FATAL_ERROR "sentry_qml_configure_android_target expected an existing target: ${target}")
    endif()

    if(Qt6_VERSION VERSION_GREATER_EQUAL 6.10)
        set_property(TARGET ${target} APPEND PROPERTY
            _qt_android_gradle_java_source_dirs "${PROJECT_SOURCE_DIR}/src/android/java"
        )
        set_property(TARGET ${target} APPEND PROPERTY
            _qt_android_gradle_implementation_dependencies "'${SENTRY_ANDROID_GRADLE_COORDINATE}'"
        )
        return()
    endif()

    get_target_property(existing_package_source_dir ${target} QT_ANDROID_PACKAGE_SOURCE_DIR)
    if(existing_package_source_dir)
        message(FATAL_ERROR
            "sentry_qml_configure_android_target(${target}) needs QT_ANDROID_PACKAGE_SOURCE_DIR "
            "for Qt ${Qt6_VERSION}, but the target already sets it. Copy "
            "${PROJECT_SOURCE_DIR}/src/android/java into the target package source directory and "
            "add '${SENTRY_ANDROID_GRADLE_COORDINATE}' to build.gradle, or build with Qt 6.10+."
        )
    endif()

    set(package_source_dir "${CMAKE_BINARY_DIR}/sentry-qml-android/${target}/package-source")
    set(package_java_dir "${package_source_dir}/src/io/sentry/qml")
    file(MAKE_DIRECTORY "${package_java_dir}")
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
