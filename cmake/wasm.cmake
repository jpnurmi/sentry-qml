if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "SENTRY_BACKEND=wasm is only supported when building with Emscripten.")
endif()

if(NOT DEFINED SENTRY_SDK_NAME)
    set(SENTRY_SDK_NAME "sentry.javascript.qml" CACHE STRING "SDK name reported by Sentry QML")
endif()

list(APPEND sentry_qml_backend_sources
    "${PROJECT_SOURCE_DIR}/src/wasm/sentrywasmsdk.cpp"
)

list(APPEND sentry_qml_backend_link_options
    -lembind
)

if(QT_FEATURE_thread)
    list(APPEND sentry_qml_backend_link_options
        -sPTHREAD_POOL_SIZE=4
    )
endif()
