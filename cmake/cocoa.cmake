if(NOT APPLE)
    message(FATAL_ERROR "SENTRY_QML_SDK=cocoa is only supported on Apple platforms.")
endif()

enable_language(OBJCXX)

set(SENTRY_COCOA_XCFRAMEWORK
    "${PROJECT_SOURCE_DIR}/modules/sentry-cocoa/SentryObjC-Dynamic.xcframework"
    CACHE PATH "Path to SentryObjC-Dynamic.xcframework"
)

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    if(CMAKE_OSX_SYSROOT MATCHES "iphonesimulator")
        set(SENTRY_COCOA_XCFRAMEWORK_SLICE "ios-arm64_x86_64-simulator")
    else()
        set(SENTRY_COCOA_XCFRAMEWORK_SLICE "ios-arm64")
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "tvOS")
    set(SENTRY_COCOA_XCFRAMEWORK_SLICE "tvos-arm64")
elseif(CMAKE_SYSTEM_NAME STREQUAL "visionOS")
    set(SENTRY_COCOA_XCFRAMEWORK_SLICE "xros-arm64")
elseif(CMAKE_SYSTEM_NAME STREQUAL "watchOS")
    set(SENTRY_COCOA_XCFRAMEWORK_SLICE "watchos-arm64_arm64_32")
else()
    set(SENTRY_COCOA_XCFRAMEWORK_SLICE "macos-arm64_x86_64")
endif()

set(SENTRY_COCOA_FRAMEWORK_DIR "${SENTRY_COCOA_XCFRAMEWORK}/${SENTRY_COCOA_XCFRAMEWORK_SLICE}")
set(SENTRY_COCOA_FRAMEWORK "${SENTRY_COCOA_FRAMEWORK_DIR}/SentryObjC.framework")

if(NOT EXISTS "${SENTRY_COCOA_FRAMEWORK}/Headers/SentryObjC.h")
    message(FATAL_ERROR
        "SentryObjC was not found at SENTRY_COCOA_XCFRAMEWORK='${SENTRY_COCOA_XCFRAMEWORK}'. "
        "Download or unpack SentryObjC-Dynamic.xcframework, or configure with "
        "-DSENTRY_COCOA_XCFRAMEWORK=/path/to/SentryObjC-Dynamic.xcframework."
    )
endif()

add_library(SentryObjC::SentryObjC INTERFACE IMPORTED GLOBAL)
target_compile_options(SentryObjC::SentryObjC INTERFACE
    "SHELL:-F ${SENTRY_COCOA_FRAMEWORK_DIR}"
)
target_link_options(SentryObjC::SentryObjC INTERFACE
    "SHELL:-F ${SENTRY_COCOA_FRAMEWORK_DIR}"
    "SHELL:-Wl,-rpath,${SENTRY_COCOA_FRAMEWORK_DIR}"
)
target_link_libraries(SentryObjC::SentryObjC INTERFACE
    "-framework SentryObjC"
    "-framework Foundation"
)

if(NOT DEFINED SENTRY_SDK_NAME)
    set(SENTRY_SDK_NAME "sentry.cocoa.qml" CACHE STRING "SDK name reported by Sentry QML")
endif()

list(APPEND SENTRY_QML_SDK_SOURCES
    "${PROJECT_SOURCE_DIR}/src/cocoa/sentryobjcbridge.mm"
    "${PROJECT_SOURCE_DIR}/src/cocoa/sentrycocoasdk.cpp"
)
list(APPEND SENTRY_QML_SDK_LIBRARIES SentryObjC::SentryObjC)

set_source_files_properties("${PROJECT_SOURCE_DIR}/src/cocoa/sentryobjcbridge.mm" PROPERTIES
    COMPILE_OPTIONS "-fobjc-arc"
)
