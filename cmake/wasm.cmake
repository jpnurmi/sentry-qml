if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "SENTRY_QML_SDK=wasm is only supported when building with Emscripten.")
endif()

if(NOT DEFINED SENTRY_SDK_NAME)
    set(SENTRY_SDK_NAME "sentry.javascript.qml" CACHE STRING "SDK name reported by Sentry QML")
endif()

list(APPEND SENTRY_QML_SDK_SOURCES
    "${PROJECT_SOURCE_DIR}/src/wasm/sentrywasmsdk.cpp"
)

list(APPEND SENTRY_QML_SDK_LINK_OPTIONS
    -lembind
)

if(QT_FEATURE_thread)
    list(APPEND SENTRY_QML_SDK_LINK_OPTIONS
        -sPTHREAD_POOL_SIZE=4
    )
endif()
