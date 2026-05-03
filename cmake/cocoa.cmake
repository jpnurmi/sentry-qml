if(NOT APPLE)
    message(FATAL_ERROR "SENTRY_QML_SDK=cocoa is only supported on Apple platforms.")
endif()

enable_language(OBJCXX)

set(SENTRY_COCOA_DIR
    "${PROJECT_SOURCE_DIR}/modules/sentry-cocoa"
    CACHE PATH "Path to the sentry-cocoa checkout"
)
set(SENTRY_COCOA_DEFAULT_XCFRAMEWORK
    "${CMAKE_BINARY_DIR}/sentry-cocoa/SentryObjC-Dynamic.xcframework"
)
set(SENTRY_COCOA_XCFRAMEWORK
    "${SENTRY_COCOA_DEFAULT_XCFRAMEWORK}"
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
set(SENTRY_COCOA_DEPENDENCY_FRAMEWORK_DIR "${SENTRY_COCOA_FRAMEWORK_DIR}/Dependencies")
set(SENTRY_COCOA_REQUIRED_FILES
    "${SENTRY_COCOA_FRAMEWORK}/Headers/SentryObjC.h"
    "${SENTRY_COCOA_FRAMEWORK}/SentryObjC"
    "${SENTRY_COCOA_DEPENDENCY_FRAMEWORK_DIR}/Sentry.framework/Sentry"
    "${SENTRY_COCOA_DEPENDENCY_FRAMEWORK_DIR}/SentryObjCBridge.framework/SentryObjCBridge"
    "${SENTRY_COCOA_DEPENDENCY_FRAMEWORK_DIR}/SentryObjCTypes.framework/SentryObjCTypes"
)

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin"
        AND SENTRY_COCOA_XCFRAMEWORK STREQUAL SENTRY_COCOA_DEFAULT_XCFRAMEWORK)
    if(NOT EXISTS "${SENTRY_COCOA_DIR}/Sentry.xcodeproj/project.pbxproj")
        message(FATAL_ERROR
            "sentry-cocoa was not found at SENTRY_COCOA_DIR='${SENTRY_COCOA_DIR}'. "
            "Initialize submodules or configure with "
            "-DSENTRY_COCOA_DIR=/path/to/sentry-cocoa."
        )
    endif()

    find_program(SENTRY_COCOA_XCODEBUILD xcodebuild REQUIRED)
    find_program(SENTRY_COCOA_LIPO lipo REQUIRED)

    set(SENTRY_COCOA_BUILD_DIR "${CMAKE_BINARY_DIR}/sentry-cocoa-build")
    set(SENTRY_COCOA_STAMP_FILE "${CMAKE_BINARY_DIR}/sentry-cocoa/SentryObjC-Dynamic.xcframework.stamp")

    add_custom_target(SentryObjCBuild
        COMMAND
            "${CMAKE_COMMAND}"
                "-DSENTRY_COCOA_SOURCE_DIR=${SENTRY_COCOA_DIR}"
                "-DSENTRY_COCOA_BUILD_DIR=${SENTRY_COCOA_BUILD_DIR}"
                "-DSENTRY_COCOA_XCFRAMEWORK=${SENTRY_COCOA_XCFRAMEWORK}"
                "-DSENTRY_COCOA_STAMP_FILE=${SENTRY_COCOA_STAMP_FILE}"
                "-DSENTRY_COCOA_XCODEBUILD=${SENTRY_COCOA_XCODEBUILD}"
                "-DSENTRY_COCOA_LIPO=${SENTRY_COCOA_LIPO}"
                "-DSENTRY_COCOA_BUILD_RECIPE=5"
                -P "${PROJECT_SOURCE_DIR}/cmake/sentry-cocoa.cmake"
        BYPRODUCTS
            "${SENTRY_COCOA_FRAMEWORK}/SentryObjC"
            "${SENTRY_COCOA_FRAMEWORK}/Headers/SentryObjC.h"
            "${SENTRY_COCOA_DEPENDENCY_FRAMEWORK_DIR}/Sentry.framework/Sentry"
            "${SENTRY_COCOA_DEPENDENCY_FRAMEWORK_DIR}/SentryObjCBridge.framework/SentryObjCBridge"
            "${SENTRY_COCOA_DEPENDENCY_FRAMEWORK_DIR}/SentryObjCTypes.framework/SentryObjCTypes"
        COMMENT "Preparing SentryObjC XCFramework"
        VERBATIM
    )
    list(APPEND SENTRY_QML_SDK_DEPENDENCIES SentryObjCBuild)
else()
    set(SENTRY_COCOA_MISSING_FILES)
    foreach(SENTRY_COCOA_REQUIRED_FILE IN LISTS SENTRY_COCOA_REQUIRED_FILES)
        if(NOT EXISTS "${SENTRY_COCOA_REQUIRED_FILE}")
            list(APPEND SENTRY_COCOA_MISSING_FILES "${SENTRY_COCOA_REQUIRED_FILE}")
        endif()
    endforeach()
    if(SENTRY_COCOA_MISSING_FILES)
        list(JOIN SENTRY_COCOA_MISSING_FILES "\n  " SENTRY_COCOA_MISSING_FILES_TEXT)
        message(FATAL_ERROR
            "SentryObjC was not found or is incomplete at "
            "SENTRY_COCOA_XCFRAMEWORK='${SENTRY_COCOA_XCFRAMEWORK}'. "
            "Missing files:\n  ${SENTRY_COCOA_MISSING_FILES_TEXT}\n"
            "Build or unpack SentryObjC-Dynamic.xcframework, or configure with "
            "-DSENTRY_COCOA_XCFRAMEWORK=/path/to/SentryObjC-Dynamic.xcframework."
        )
    endif()
endif()

add_library(SentryObjC::SentryObjC INTERFACE IMPORTED GLOBAL)
target_compile_options(SentryObjC::SentryObjC INTERFACE
    "-F${SENTRY_COCOA_FRAMEWORK_DIR}"
)
target_link_options(SentryObjC::SentryObjC INTERFACE
    "-F${SENTRY_COCOA_FRAMEWORK_DIR}"
    "-F${SENTRY_COCOA_DEPENDENCY_FRAMEWORK_DIR}"
    "-Wl,-rpath,${SENTRY_COCOA_FRAMEWORK_DIR}"
    "-Wl,-rpath,${SENTRY_COCOA_DEPENDENCY_FRAMEWORK_DIR}"
)
target_link_libraries(SentryObjC::SentryObjC INTERFACE
    "-framework SentryObjC"
    "-framework Sentry"
    "-framework SentryObjCBridge"
    "-framework SentryObjCTypes"
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
