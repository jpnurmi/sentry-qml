foreach(required_var IN ITEMS
        SENTRY_COCOA_SOURCE_DIR
        SENTRY_COCOA_BUILD_DIR
        SENTRY_COCOA_XCFRAMEWORK
        SENTRY_COCOA_STAMP_FILE
        SENTRY_COCOA_XCODEBUILD
        SENTRY_COCOA_LIPO
        SENTRY_COCOA_BUILD_RECIPE)
    if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
        message(FATAL_ERROR "${required_var} is required.")
    endif()
endforeach()

set(sentry_cocoa_framework_dir
    "${SENTRY_COCOA_XCFRAMEWORK}/macos-arm64_x86_64"
)
set(sentry_cocoa_dependency_framework_dir "${sentry_cocoa_framework_dir}/Dependencies")
set(sentry_cocoa_frameworks
    Sentry
    SentryObjC
    SentryObjCBridge
    SentryObjCTypes
)
set(sentry_cocoa_required_outputs
    "${sentry_cocoa_framework_dir}/SentryObjC.framework/SentryObjC"
    "${sentry_cocoa_framework_dir}/SentryObjC.framework/Headers/SentryObjC.h"
    "${sentry_cocoa_dependency_framework_dir}/Sentry.framework/Sentry"
    "${sentry_cocoa_dependency_framework_dir}/SentryObjCBridge.framework/SentryObjCBridge"
    "${sentry_cocoa_dependency_framework_dir}/SentryObjCTypes.framework/SentryObjCTypes"
)

execute_process(
    COMMAND git -C "${SENTRY_COCOA_SOURCE_DIR}" rev-parse HEAD
    OUTPUT_VARIABLE sentry_cocoa_revision
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE sentry_cocoa_git_result
)
if(NOT sentry_cocoa_git_result EQUAL 0)
    set(sentry_cocoa_revision "unknown")
endif()

execute_process(
    COMMAND git -C "${SENTRY_COCOA_SOURCE_DIR}" status --short
    OUTPUT_VARIABLE sentry_cocoa_status
    ERROR_QUIET
    RESULT_VARIABLE sentry_cocoa_status_result
)
if(NOT sentry_cocoa_status_result EQUAL 0)
    set(sentry_cocoa_status "")
endif()
string(SHA256 sentry_cocoa_status_hash "${sentry_cocoa_status}")

string(CONCAT sentry_cocoa_expected_stamp
    "recipe=${SENTRY_COCOA_BUILD_RECIPE}\n"
    "revision=${sentry_cocoa_revision}\n"
    "status=${sentry_cocoa_status_hash}\n"
    "architectures=arm64,x86_64\n"
)

set(sentry_cocoa_outputs_exist TRUE)
foreach(sentry_cocoa_required_output IN LISTS sentry_cocoa_required_outputs)
    if(NOT EXISTS "${sentry_cocoa_required_output}")
        set(sentry_cocoa_outputs_exist FALSE)
    endif()
endforeach()

if(sentry_cocoa_outputs_exist AND EXISTS "${SENTRY_COCOA_STAMP_FILE}")
    file(READ "${SENTRY_COCOA_STAMP_FILE}" sentry_cocoa_actual_stamp)
    if(sentry_cocoa_actual_stamp STREQUAL sentry_cocoa_expected_stamp)
        message(STATUS "SentryObjC XCFramework is up to date")
        return()
    endif()
endif()

function(run_sentry_cocoa_command)
    execute_process(
        COMMAND
            "${CMAKE_COMMAND}" -E env
            "HOME=${sentry_cocoa_home_dir}"
            "PATH=${sentry_cocoa_tools_dir}:$ENV{PATH}"
            ${ARGV}
        WORKING_DIRECTORY "${SENTRY_COCOA_BUILD_DIR}"
        RESULT_VARIABLE sentry_cocoa_command_result
    )
    if(NOT sentry_cocoa_command_result EQUAL 0)
        message(FATAL_ERROR
            "SentryObjC build command failed with exit code "
            "${sentry_cocoa_command_result}."
        )
    endif()
endfunction()

message(STATUS "Building SentryObjC XCFramework")

file(REMOVE_RECURSE "${SENTRY_COCOA_BUILD_DIR}" "${SENTRY_COCOA_XCFRAMEWORK}")
get_filename_component(sentry_cocoa_output_dir "${SENTRY_COCOA_XCFRAMEWORK}" DIRECTORY)
set(sentry_cocoa_tools_dir "${SENTRY_COCOA_BUILD_DIR}/tools")
set(sentry_cocoa_home_dir "${SENTRY_COCOA_BUILD_DIR}/home")
file(MAKE_DIRECTORY
    "${SENTRY_COCOA_BUILD_DIR}"
    "${sentry_cocoa_output_dir}"
    "${sentry_cocoa_tools_dir}"
    "${sentry_cocoa_home_dir}"
)

file(WRITE "${sentry_cocoa_tools_dir}/swiftlint" "#!/bin/sh\nexit 0\n")
file(WRITE "${sentry_cocoa_tools_dir}/uname"
    "#!/bin/sh\n"
    "if [ \"$1\" = \"-m\" ]; then\n"
    "    echo x86_64\n"
    "else\n"
    "    /usr/bin/uname \"$@\"\n"
    "fi\n"
)
file(CHMOD
    "${sentry_cocoa_tools_dir}/swiftlint"
    "${sentry_cocoa_tools_dir}/uname"
    PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE
)

foreach(sentry_cocoa_arch IN ITEMS arm64 x86_64)
    run_sentry_cocoa_command(
        "${SENTRY_COCOA_XCODEBUILD}" -quiet archive
        -project "${SENTRY_COCOA_SOURCE_DIR}/Sentry.xcodeproj"
        -scheme SentryObjC
        -configuration Release
        -sdk macosx
        -destination "generic/platform=macOS"
        -derivedDataPath "${SENTRY_COCOA_BUILD_DIR}/DerivedData-${sentry_cocoa_arch}"
        -archivePath "${SENTRY_COCOA_BUILD_DIR}/macosx-${sentry_cocoa_arch}.xcarchive"
        CODE_SIGNING_REQUIRED=NO
        SKIP_INSTALL=NO
        CODE_SIGN_IDENTITY=
        MACH_O_TYPE=mh_dylib
        "ARCHS=${sentry_cocoa_arch}"
        ONLY_ACTIVE_ARCH=NO
        ENABLE_CODE_COVERAGE=NO
        GCC_GENERATE_DEBUGGING_SYMBOLS=NO
    )
endforeach()

set(sentry_cocoa_universal_root "${SENTRY_COCOA_BUILD_DIR}/universal")
file(MAKE_DIRECTORY "${sentry_cocoa_universal_root}")

foreach(sentry_cocoa_framework_name IN LISTS sentry_cocoa_frameworks)
    set(sentry_cocoa_arm64_framework
        "${SENTRY_COCOA_BUILD_DIR}/macosx-arm64.xcarchive/Products/Library/Frameworks/${sentry_cocoa_framework_name}.framework"
    )
    set(sentry_cocoa_x86_64_framework
        "${SENTRY_COCOA_BUILD_DIR}/macosx-x86_64.xcarchive/Products/Library/Frameworks/${sentry_cocoa_framework_name}.framework"
    )
    set(sentry_cocoa_universal_framework
        "${sentry_cocoa_universal_root}/${sentry_cocoa_framework_name}.framework"
    )

    file(COPY "${sentry_cocoa_arm64_framework}" DESTINATION "${sentry_cocoa_universal_root}")

    run_sentry_cocoa_command(
        "${SENTRY_COCOA_LIPO}" -create
        "${sentry_cocoa_arm64_framework}/${sentry_cocoa_framework_name}"
        "${sentry_cocoa_x86_64_framework}/${sentry_cocoa_framework_name}"
        -output "${SENTRY_COCOA_BUILD_DIR}/${sentry_cocoa_framework_name}"
    )
    file(COPY
        "${SENTRY_COCOA_BUILD_DIR}/${sentry_cocoa_framework_name}"
        DESTINATION "${sentry_cocoa_universal_framework}"
    )

    if(EXISTS "${sentry_cocoa_x86_64_framework}/Modules")
        file(COPY
            "${sentry_cocoa_x86_64_framework}/Modules/"
            DESTINATION "${sentry_cocoa_universal_framework}/Modules"
        )
    endif()

    run_sentry_cocoa_command(
        "${SENTRY_COCOA_LIPO}"
        "${sentry_cocoa_universal_framework}/${sentry_cocoa_framework_name}"
        -verify_arch arm64 x86_64
    )
endforeach()

file(GLOB sentry_cocoa_type_headers
    "${SENTRY_COCOA_SOURCE_DIR}/Sources/SentryObjCTypes/Public/*.h"
)
if(sentry_cocoa_type_headers)
    file(COPY
        ${sentry_cocoa_type_headers}
        DESTINATION "${sentry_cocoa_universal_root}/SentryObjC.framework/Headers"
    )
endif()

run_sentry_cocoa_command(
    "${SENTRY_COCOA_XCODEBUILD}" -create-xcframework
    -framework "${sentry_cocoa_universal_root}/SentryObjC.framework"
    -output "${SENTRY_COCOA_XCFRAMEWORK}"
)

file(MAKE_DIRECTORY "${sentry_cocoa_dependency_framework_dir}")
foreach(sentry_cocoa_framework_name IN ITEMS Sentry SentryObjCBridge SentryObjCTypes)
    file(COPY
        "${sentry_cocoa_universal_root}/${sentry_cocoa_framework_name}.framework"
        DESTINATION "${sentry_cocoa_dependency_framework_dir}"
    )
endforeach()

foreach(sentry_cocoa_required_output IN LISTS sentry_cocoa_required_outputs)
    if(sentry_cocoa_required_output MATCHES "/Headers/")
        continue()
    endif()
    run_sentry_cocoa_command(
        "${SENTRY_COCOA_LIPO}"
        "${sentry_cocoa_required_output}"
        -verify_arch arm64 x86_64
    )
endforeach()

file(WRITE "${SENTRY_COCOA_STAMP_FILE}" "${sentry_cocoa_expected_stamp}")
