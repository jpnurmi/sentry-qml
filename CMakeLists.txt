cmake_minimum_required(VERSION 3.21)

project(SentryQml
    VERSION 0.1.0
    DESCRIPTION "Sentry SDK bindings for Qt/QML"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Qml Quick QuickControls2 Test)
find_package(Qt6 6.8 QUIET COMPONENTS QmlPrivate)

qt_standard_project_setup(REQUIRES 6.8)

include(GNUInstallDirs)
include(CTest)

set(SENTRY_QML_IMPORT_URI Sentry)
set(SENTRY_QML_IMPORT_VERSION 1.0)
set(SENTRY_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml" CACHE PATH
    "Build-tree QML import root for Sentry QML modules"
)
set(QT_QML_OUTPUT_DIRECTORY "${SENTRY_QML_OUTPUT_DIRECTORY}")
set(QML_IMPORT_PATH "${SENTRY_QML_OUTPUT_DIRECTORY}" CACHE STRING
    "QML import paths for Qt Creator and other QML tooling"
)

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

if(NOT DEFINED SENTRY_BACKEND)
    set(SENTRY_BACKEND "native" CACHE STRING
        "sentry-native crash backend. Use 'crashpad', 'breakpad', 'inproc', 'native', or 'none'."
    )
endif()

if(NOT DEFINED SENTRY_SDK_NAME)
    set(SENTRY_SDK_NAME "sentry.native.qml" CACHE STRING "SDK name reported by sentry-native")
endif()

add_subdirectory("${SENTRY_NATIVE_DIR}" "${CMAKE_BINARY_DIR}/sentry-native")

if(TARGET sentry-crash)
    target_sources(sentry-crash PRIVATE src/sentryqtlogging.cpp)
    target_link_libraries(sentry-crash PRIVATE Qt6::Core)
endif()

add_subdirectory(src)
add_subdirectory(example)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
