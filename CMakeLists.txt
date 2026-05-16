cmake_minimum_required(VERSION 3.21)

project(SentryQml
    VERSION 0.1.0
    DESCRIPTION "Sentry SDK bindings for Qt/QML"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Network Qml Quick)
find_package(Qt6QuickControls2 6.8 QUIET)
find_package(Qt6QmlPrivate 6.8 QUIET)
find_package(Qt6Test 6.8 QUIET)

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

set(sentry_qml_backend_supported native cocoa android wasm)
set(sentry_native_backend_supported crashpad breakpad inproc native none)
set(sentry_backend_supported
    ${sentry_qml_backend_supported}
    ${sentry_native_backend_supported}
)
list(REMOVE_DUPLICATES sentry_backend_supported)
list(JOIN sentry_backend_supported ", " sentry_backend_supported_text)

set(sentry_default_backend native)
if(ANDROID)
    set(sentry_default_backend android)
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(sentry_default_backend cocoa)
elseif(EMSCRIPTEN)
    set(sentry_default_backend wasm)
endif()

set(SENTRY_BACKEND "${sentry_default_backend}" CACHE STRING "Sentry backend implementation")
set_property(CACHE SENTRY_BACKEND PROPERTY STRINGS ${sentry_backend_supported})

string(TOLOWER "${SENTRY_BACKEND}" SENTRY_BACKEND)
set(sentry_qml_backend "${SENTRY_BACKEND}")
if(sentry_qml_backend STREQUAL "ios")
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
        message(FATAL_ERROR
            "SENTRY_BACKEND=ios is deprecated and only accepted when building for iOS. "
            "Use SENTRY_BACKEND=cocoa for Sentry Cocoa."
        )
    endif()
    message(DEPRECATION
        "SENTRY_BACKEND=ios is deprecated. Use SENTRY_BACKEND=cocoa; "
        "the iOS target platform is inferred from CMAKE_SYSTEM_NAME."
    )
    set(sentry_qml_backend cocoa)
elseif(SENTRY_BACKEND IN_LIST sentry_native_backend_supported)
    set(sentry_qml_backend native)
endif()

if(NOT sentry_qml_backend IN_LIST sentry_qml_backend_supported)
    message(FATAL_ERROR
        "Unsupported SENTRY_BACKEND value '${SENTRY_BACKEND}' "
        "(supported values: ${sentry_backend_supported_text})."
    )
endif()
if(sentry_qml_backend STREQUAL "native" AND NOT SENTRY_BACKEND STREQUAL "native")
    message(STATUS "Sentry backend: native (${SENTRY_BACKEND})")
else()
    message(STATUS "Sentry backend: ${sentry_qml_backend}")
endif()

set(sentry_qml_backend_sources)
set(sentry_qml_backend_libraries)
set(sentry_qml_backend_include_directories)
set(sentry_qml_backend_dependencies)
set(sentry_qml_backend_link_options)

include("${PROJECT_SOURCE_DIR}/cmake/${sentry_qml_backend}.cmake")

add_subdirectory(src)

if(TARGET Qt6::QuickControls2)
    add_subdirectory(example)
endif()

if(BUILD_TESTING AND TARGET Qt6::Test)
    add_subdirectory(tests)
endif()
