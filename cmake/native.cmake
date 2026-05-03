set(SENTRY_NATIVE_DIR "${PROJECT_SOURCE_DIR}/modules/sentry-native" CACHE PATH
    "Path to the sentry-native checkout, normally modules/sentry-native"
)

if(NOT EXISTS "${SENTRY_NATIVE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "sentry-native was not found at SENTRY_NATIVE_DIR='${SENTRY_NATIVE_DIR}'. "
        "Run git submodule update --init --recursive or configure with "
        "-DSENTRY_NATIVE_DIR=/path/to/sentry-native."
    )
endif()

set(SENTRY_BUILD_TESTS OFF CACHE BOOL "Build sentry-native tests" FORCE)
set(SENTRY_BUILD_EXAMPLES OFF CACHE BOOL "Build sentry-native examples" FORCE)
set(SENTRY_BUILD_BENCHMARKS OFF CACHE BOOL "Build sentry-native benchmarks" FORCE)

if(NOT DEFINED SENTRY_TRANSPORT)
    set(SENTRY_TRANSPORT "custom" CACHE STRING
        "sentry-native HTTP transport. Sentry QML provides a Qt transport."
    )
endif()

if(NOT DEFINED SENTRY_BACKEND)
    set(SENTRY_BACKEND "native" CACHE STRING
        "sentry-native crash backend. Use 'crashpad', 'breakpad', 'inproc', 'native', or 'none'."
    )
endif()

if(NOT DEFINED SENTRY_SDK_NAME)
    set(SENTRY_SDK_NAME "sentry.native.qml" CACHE STRING "SDK name reported by sentry-native")
endif()

list(APPEND SENTRY_QML_SDK_SOURCES "${PROJECT_SOURCE_DIR}/src/native/sentrynativesdk.cpp")
list(APPEND SENTRY_QML_SDK_LIBRARIES sentry::sentry)
list(APPEND SENTRY_QML_SDK_INCLUDE_DIRECTORIES "${SENTRY_NATIVE_DIR}")

add_subdirectory("${SENTRY_NATIVE_DIR}" "${CMAKE_BINARY_DIR}/sentry-native")

if(TARGET sentry-crash)
    target_sources(sentry-crash PRIVATE "${PROJECT_SOURCE_DIR}/src/native/sentryqtlogging.cpp")
    target_link_libraries(sentry-crash PRIVATE Qt6::Core)
endif()

if(SENTRY_TRANSPORT STREQUAL "custom")
    set(SENTRY_QML_QT_TRANSPORT_SOURCE "${PROJECT_SOURCE_DIR}/src/native/sentryqttransport.cpp")

    target_sources(sentry PRIVATE "${SENTRY_QML_QT_TRANSPORT_SOURCE}")
    target_include_directories(sentry PRIVATE "${SENTRY_NATIVE_DIR}")
    target_link_libraries(sentry PRIVATE Qt6::Core Qt6::Network)

    if(WIN32)
        target_compile_definitions(sentry PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    endif()

    if(TARGET sentry-crash)
        target_sources(sentry-crash PRIVATE "${SENTRY_QML_QT_TRANSPORT_SOURCE}")
        target_include_directories(sentry-crash PRIVATE "${SENTRY_NATIVE_DIR}")
        target_link_libraries(sentry-crash PRIVATE Qt6::Core Qt6::Network)

        if(WIN32)
            target_compile_definitions(sentry-crash PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        endif()
    endif()
endif()
