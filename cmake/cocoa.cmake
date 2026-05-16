if(NOT APPLE)
    message(FATAL_ERROR "SENTRY_BACKEND=cocoa is only supported on Apple platforms.")
endif()

enable_language(OBJCXX)

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(SENTRY_IOS_MINIMUM_DEPLOYMENT_TARGET "15.0" CACHE STRING
        "Minimum iOS deployment target supported by Sentry Cocoa"
    )

    if(CMAKE_OSX_DEPLOYMENT_TARGET
            AND CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS SENTRY_IOS_MINIMUM_DEPLOYMENT_TARGET)
        message(FATAL_ERROR
            "SENTRY_BACKEND=cocoa requires CMAKE_OSX_DEPLOYMENT_TARGET "
            "${SENTRY_IOS_MINIMUM_DEPLOYMENT_TARGET} or higher when building for iOS."
        )
    endif()

    if(NOT CMAKE_XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET)
        set(CMAKE_XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "${SENTRY_IOS_MINIMUM_DEPLOYMENT_TARGET}")
    endif()
endif()

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
)

set(SENTRY_COCOA_BUILD_FROM_SOURCE FALSE)
set(SENTRY_COCOA_BUILD_SDKS)
if(SENTRY_COCOA_XCFRAMEWORK STREQUAL SENTRY_COCOA_DEFAULT_XCFRAMEWORK)
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(SENTRY_COCOA_BUILD_FROM_SOURCE TRUE)
        set(SENTRY_COCOA_BUILD_SDKS macosx)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set(SENTRY_COCOA_BUILD_FROM_SOURCE TRUE)
        set(SENTRY_COCOA_BUILD_SDKS iphoneos iphonesimulator)
    endif()
endif()

set(SENTRY_COCOA_EXPECT_DEPENDENCY_FRAMEWORKS FALSE)
if(SENTRY_COCOA_BUILD_FROM_SOURCE)
    set(SENTRY_COCOA_EXPECT_DEPENDENCY_FRAMEWORKS TRUE)
elseif(EXISTS "${SENTRY_COCOA_DEPENDENCY_FRAMEWORK_DIR}")
    set(SENTRY_COCOA_EXPECT_DEPENDENCY_FRAMEWORKS TRUE)
endif()

set(SENTRY_COCOA_DEPENDENCY_FRAMEWORKS)
foreach(SENTRY_COCOA_DEPENDENCY_FRAMEWORK_NAME IN ITEMS Sentry SentryObjCBridge SentryObjCTypes)
    set(SENTRY_COCOA_DEPENDENCY_FRAMEWORK
        "${SENTRY_COCOA_DEPENDENCY_FRAMEWORK_DIR}/${SENTRY_COCOA_DEPENDENCY_FRAMEWORK_NAME}.framework"
    )
    if(SENTRY_COCOA_EXPECT_DEPENDENCY_FRAMEWORKS)
        list(APPEND SENTRY_COCOA_DEPENDENCY_FRAMEWORKS "${SENTRY_COCOA_DEPENDENCY_FRAMEWORK}")
        list(APPEND SENTRY_COCOA_REQUIRED_FILES
            "${SENTRY_COCOA_DEPENDENCY_FRAMEWORK}/${SENTRY_COCOA_DEPENDENCY_FRAMEWORK_NAME}"
        )
    endif()
endforeach()
set(SENTRY_COCOA_EMBED_FRAMEWORKS "${SENTRY_COCOA_FRAMEWORK}" ${SENTRY_COCOA_DEPENDENCY_FRAMEWORKS})

if(SENTRY_COCOA_BUILD_FROM_SOURCE)
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
    list(JOIN SENTRY_COCOA_BUILD_SDKS "," SENTRY_COCOA_BUILD_SDKS_TEXT)

    add_custom_target(SentryObjCBuild
        COMMAND
            "${CMAKE_COMMAND}"
                "-DSENTRY_COCOA_SOURCE_DIR=${SENTRY_COCOA_DIR}"
                "-DSENTRY_COCOA_BUILD_DIR=${SENTRY_COCOA_BUILD_DIR}"
                "-DSENTRY_COCOA_XCFRAMEWORK=${SENTRY_COCOA_XCFRAMEWORK}"
                "-DSENTRY_COCOA_STAMP_FILE=${SENTRY_COCOA_STAMP_FILE}"
                "-DSENTRY_COCOA_XCODEBUILD=${SENTRY_COCOA_XCODEBUILD}"
                "-DSENTRY_COCOA_LIPO=${SENTRY_COCOA_LIPO}"
                "-DSENTRY_COCOA_SDKS=${SENTRY_COCOA_BUILD_SDKS_TEXT}"
                "-DSENTRY_COCOA_IOS_DEPLOYMENT_TARGET=${SENTRY_IOS_MINIMUM_DEPLOYMENT_TARGET}"
                "-DSENTRY_COCOA_BUILD_RECIPE=6"
                -P "${PROJECT_SOURCE_DIR}/cmake/sentry-cocoa.cmake"
        BYPRODUCTS
            ${SENTRY_COCOA_REQUIRED_FILES}
        COMMENT "Preparing SentryObjC XCFramework"
        VERBATIM
    )
    list(APPEND sentry_qml_backend_dependencies SentryObjCBuild)
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
            "Build or unpack SentryObjC-Dynamic.xcframework from sentry-cocoa, "
            "or configure with "
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
    "-Wl,-rpath,${SENTRY_COCOA_FRAMEWORK_DIR}"
)
if(SENTRY_COCOA_DEPENDENCY_FRAMEWORKS)
    target_link_options(SentryObjC::SentryObjC INTERFACE
        "-F${SENTRY_COCOA_DEPENDENCY_FRAMEWORK_DIR}"
        "-Wl,-rpath,${SENTRY_COCOA_DEPENDENCY_FRAMEWORK_DIR}"
    )
endif()
target_link_libraries(SentryObjC::SentryObjC INTERFACE
    "-framework SentryObjC"
    "-framework Foundation"
)
if(SENTRY_COCOA_DEPENDENCY_FRAMEWORKS)
    target_link_libraries(SentryObjC::SentryObjC INTERFACE
        "-framework Sentry"
        "-framework SentryObjCBridge"
        "-framework SentryObjCTypes"
    )
endif()

if(NOT DEFINED SENTRY_SDK_NAME)
    set(SENTRY_SDK_NAME "sentry.cocoa.qml" CACHE STRING "SDK name reported by Sentry QML")
endif()

list(APPEND sentry_qml_backend_sources
    "${PROJECT_SOURCE_DIR}/src/cocoa/sentryobjcbridge.mm"
    "${PROJECT_SOURCE_DIR}/src/cocoa/sentrycocoasdk.cpp"
)
list(APPEND sentry_qml_backend_libraries SentryObjC::SentryObjC)

set_source_files_properties("${PROJECT_SOURCE_DIR}/src/cocoa/sentryobjcbridge.mm" PROPERTIES
    COMPILE_OPTIONS "-fobjc-arc"
)

function(sentry_qml_configure_ios_target target)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
        return()
    endif()

    if(NOT TARGET ${target})
        message(FATAL_ERROR "sentry_qml_configure_ios_target expected an existing target: ${target}")
    endif()

    if(NOT CMAKE_GENERATOR STREQUAL "Xcode")
        message(FATAL_ERROR
            "sentry_qml_configure_ios_target(${target}) requires the Xcode generator "
            "so CMake can add Sentry Cocoa to the Embed Frameworks build phase."
        )
    endif()

    get_target_property(existing_embed_frameworks ${target} XCODE_EMBED_FRAMEWORKS)
    if(NOT existing_embed_frameworks)
        set(existing_embed_frameworks)
    endif()

    set_property(TARGET ${target} PROPERTY XCODE_EMBED_FRAMEWORKS
        ${existing_embed_frameworks}
        ${SENTRY_COCOA_EMBED_FRAMEWORKS}
    )
    set_property(TARGET ${target} PROPERTY XCODE_EMBED_FRAMEWORKS_CODE_SIGN_ON_COPY YES)
    set_property(TARGET ${target} PROPERTY XCODE_EMBED_FRAMEWORKS_REMOVE_HEADERS_ON_COPY YES)

    get_target_property(existing_deployment_target ${target} XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET)
    if(NOT existing_deployment_target AND DEFINED SENTRY_IOS_MINIMUM_DEPLOYMENT_TARGET)
        set_property(TARGET ${target} PROPERTY
            XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "${SENTRY_IOS_MINIMUM_DEPLOYMENT_TARGET}"
        )
    endif()
endfunction()
