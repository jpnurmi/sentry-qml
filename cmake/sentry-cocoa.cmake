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

if(NOT DEFINED SENTRY_COCOA_SDKS OR "${SENTRY_COCOA_SDKS}" STREQUAL "")
    set(SENTRY_COCOA_SDKS macosx)
endif()
if(NOT DEFINED SENTRY_COCOA_IOS_DEPLOYMENT_TARGET
        OR "${SENTRY_COCOA_IOS_DEPLOYMENT_TARGET}" STREQUAL "")
    set(SENTRY_COCOA_IOS_DEPLOYMENT_TARGET "15.0")
endif()
string(REPLACE "," ";" sentry_cocoa_sdks "${SENTRY_COCOA_SDKS}")

set(sentry_cocoa_frameworks
    Sentry
    SentryObjC
    SentryObjCBridge
    SentryObjCTypes
)
set(sentry_cocoa_dependency_frameworks
    Sentry
    SentryObjCBridge
    SentryObjCTypes
)

function(configure_sentry_cocoa_sdk sdk out_slice out_architectures out_destination)
    if(sdk STREQUAL "macosx")
        set(slice "macos-arm64_x86_64")
        set(architectures arm64 x86_64)
        set(destination "generic/platform=macOS")
    elseif(sdk STREQUAL "iphoneos")
        set(slice "ios-arm64")
        set(architectures arm64)
        set(destination "generic/platform=iOS")
    elseif(sdk STREQUAL "iphonesimulator")
        set(slice "ios-arm64_x86_64-simulator")
        set(architectures arm64 x86_64)
        set(destination "generic/platform=iOS Simulator")
    else()
        message(FATAL_ERROR "Unsupported Sentry Cocoa SDK '${sdk}'.")
    endif()

    set(${out_slice} "${slice}" PARENT_SCOPE)
    set(${out_architectures} "${architectures}" PARENT_SCOPE)
    set(${out_destination} "${destination}" PARENT_SCOPE)
endfunction()

function(sentry_cocoa_framework_binary out_binary framework_dir framework_name)
    if(EXISTS "${framework_dir}/Versions/A/${framework_name}")
        set(binary "${framework_dir}/Versions/A/${framework_name}")
    else()
        set(binary "${framework_dir}/${framework_name}")
    endif()
    set(${out_binary} "${binary}" PARENT_SCOPE)
endfunction()

function(copy_sentry_cocoa_modules source_framework dest_framework)
    if(EXISTS "${source_framework}/Modules")
        file(MAKE_DIRECTORY "${dest_framework}/Modules")
        file(COPY "${source_framework}/Modules/" DESTINATION "${dest_framework}/Modules")
    endif()
    if(EXISTS "${source_framework}/Versions/A/Modules")
        file(MAKE_DIRECTORY "${dest_framework}/Versions/A/Modules")
        file(COPY "${source_framework}/Versions/A/Modules/"
            DESTINATION "${dest_framework}/Versions/A/Modules"
        )
    endif()
endfunction()

function(copy_sentry_cocoa_type_headers sentry_objc_framework)
    if(EXISTS "${sentry_objc_framework}/Versions/A/Headers")
        set(headers_dir "${sentry_objc_framework}/Versions/A/Headers")
    else()
        set(headers_dir "${sentry_objc_framework}/Headers")
    endif()

    file(GLOB sentry_cocoa_type_headers
        "${SENTRY_COCOA_SOURCE_DIR}/Sources/SentryObjCTypes/Public/*.h"
    )
    if(sentry_cocoa_type_headers)
        file(COPY ${sentry_cocoa_type_headers} DESTINATION "${headers_dir}")
    endif()
endfunction()

set(sentry_cocoa_required_outputs)
set(sentry_cocoa_stamp_slices)
foreach(sentry_cocoa_sdk IN LISTS sentry_cocoa_sdks)
    configure_sentry_cocoa_sdk("${sentry_cocoa_sdk}"
        sentry_cocoa_slice
        sentry_cocoa_architectures
        sentry_cocoa_destination
    )
    string(REPLACE ";" "+" sentry_cocoa_architectures_text "${sentry_cocoa_architectures}")
    list(APPEND sentry_cocoa_stamp_slices
        "${sentry_cocoa_sdk}:${sentry_cocoa_slice}:${sentry_cocoa_architectures_text}"
    )

    set(sentry_cocoa_framework_dir "${SENTRY_COCOA_XCFRAMEWORK}/${sentry_cocoa_slice}")
    set(sentry_cocoa_dependency_framework_dir "${sentry_cocoa_framework_dir}/Dependencies")
    list(APPEND sentry_cocoa_required_outputs
        "${sentry_cocoa_framework_dir}/SentryObjC.framework/SentryObjC"
        "${sentry_cocoa_framework_dir}/SentryObjC.framework/Headers/SentryObjC.h"
    )
    foreach(sentry_cocoa_framework_name IN LISTS sentry_cocoa_dependency_frameworks)
        list(APPEND sentry_cocoa_required_outputs
            "${sentry_cocoa_dependency_framework_dir}/${sentry_cocoa_framework_name}.framework/${sentry_cocoa_framework_name}"
        )
    endforeach()
endforeach()

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
list(JOIN sentry_cocoa_stamp_slices "," sentry_cocoa_stamp_slices_text)

string(CONCAT sentry_cocoa_expected_stamp
    "recipe=${SENTRY_COCOA_BUILD_RECIPE}\n"
    "revision=${sentry_cocoa_revision}\n"
    "status=${sentry_cocoa_status_hash}\n"
    "slices=${sentry_cocoa_stamp_slices_text}\n"
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
set(sentry_cocoa_archives_dir "${SENTRY_COCOA_BUILD_DIR}/archives")
set(sentry_cocoa_slices_dir "${SENTRY_COCOA_BUILD_DIR}/slices")
set(sentry_cocoa_source_packages_dir "${SENTRY_COCOA_BUILD_DIR}/SourcePackages")
set(sentry_cocoa_tools_dir "${SENTRY_COCOA_BUILD_DIR}/tools")
set(sentry_cocoa_home_dir "${SENTRY_COCOA_BUILD_DIR}/home")
file(MAKE_DIRECTORY
    "${SENTRY_COCOA_BUILD_DIR}"
    "${sentry_cocoa_archives_dir}"
    "${sentry_cocoa_slices_dir}"
    "${sentry_cocoa_source_packages_dir}"
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

set(sentry_cocoa_create_xcframework_args)
set(sentry_cocoa_built_slice_names)
set(sentry_cocoa_built_slice_dirs)
foreach(sentry_cocoa_sdk IN LISTS sentry_cocoa_sdks)
    configure_sentry_cocoa_sdk("${sentry_cocoa_sdk}"
        sentry_cocoa_slice
        sentry_cocoa_architectures
        sentry_cocoa_destination
    )
    set(sentry_cocoa_slice_build_dir "${sentry_cocoa_slices_dir}/${sentry_cocoa_sdk}")
    file(MAKE_DIRECTORY "${sentry_cocoa_slice_build_dir}")

    set(sentry_cocoa_archives)
    foreach(sentry_cocoa_arch IN LISTS sentry_cocoa_architectures)
        set(sentry_cocoa_archive_path
            "${sentry_cocoa_archives_dir}/${sentry_cocoa_sdk}-${sentry_cocoa_arch}.xcarchive"
        )
        list(APPEND sentry_cocoa_archives "${sentry_cocoa_archive_path}")
        run_sentry_cocoa_command(
            "${SENTRY_COCOA_XCODEBUILD}" -quiet archive
            -project "${SENTRY_COCOA_SOURCE_DIR}/Sentry.xcodeproj"
            -scheme SentryObjC
            -configuration Release
            -sdk "${sentry_cocoa_sdk}"
            -destination "${sentry_cocoa_destination}"
            -derivedDataPath "${SENTRY_COCOA_BUILD_DIR}/DerivedData-${sentry_cocoa_sdk}-${sentry_cocoa_arch}"
            -archivePath "${sentry_cocoa_archive_path}"
            -clonedSourcePackagesDirPath "${sentry_cocoa_source_packages_dir}"
            -skipPackagePluginValidation
            -skipMacroValidation
            CODE_SIGNING_ALLOWED=NO
            CODE_SIGNING_REQUIRED=NO
            SKIP_INSTALL=NO
            CODE_SIGN_IDENTITY=
            DEVELOPMENT_TEAM=
            MACH_O_TYPE=mh_dylib
            "ARCHS=${sentry_cocoa_arch}"
            ONLY_ACTIVE_ARCH=NO
            ENABLE_CODE_COVERAGE=NO
            GCC_GENERATE_DEBUGGING_SYMBOLS=NO
            "IPHONEOS_DEPLOYMENT_TARGET=${SENTRY_COCOA_IOS_DEPLOYMENT_TARGET}"
        )
    endforeach()

    foreach(sentry_cocoa_framework_name IN LISTS sentry_cocoa_frameworks)
        list(GET sentry_cocoa_archives 0 sentry_cocoa_first_archive)
        set(sentry_cocoa_first_framework
            "${sentry_cocoa_first_archive}/Products/Library/Frameworks/${sentry_cocoa_framework_name}.framework"
        )
        set(sentry_cocoa_slice_framework
            "${sentry_cocoa_slice_build_dir}/${sentry_cocoa_framework_name}.framework"
        )
        file(COPY "${sentry_cocoa_first_framework}" DESTINATION "${sentry_cocoa_slice_build_dir}")

        set(sentry_cocoa_arch_binaries)
        foreach(sentry_cocoa_archive IN LISTS sentry_cocoa_archives)
            set(sentry_cocoa_arch_framework
                "${sentry_cocoa_archive}/Products/Library/Frameworks/${sentry_cocoa_framework_name}.framework"
            )
            sentry_cocoa_framework_binary(
                sentry_cocoa_arch_binary
                "${sentry_cocoa_arch_framework}"
                "${sentry_cocoa_framework_name}"
            )
            list(APPEND sentry_cocoa_arch_binaries "${sentry_cocoa_arch_binary}")
            copy_sentry_cocoa_modules("${sentry_cocoa_arch_framework}" "${sentry_cocoa_slice_framework}")
        endforeach()

        sentry_cocoa_framework_binary(
            sentry_cocoa_slice_binary
            "${sentry_cocoa_slice_framework}"
            "${sentry_cocoa_framework_name}"
        )
        run_sentry_cocoa_command(
            "${SENTRY_COCOA_LIPO}" -create
            ${sentry_cocoa_arch_binaries}
            -output "${SENTRY_COCOA_BUILD_DIR}/${sentry_cocoa_framework_name}-${sentry_cocoa_sdk}"
        )
        configure_file(
            "${SENTRY_COCOA_BUILD_DIR}/${sentry_cocoa_framework_name}-${sentry_cocoa_sdk}"
            "${sentry_cocoa_slice_binary}"
            COPYONLY
        )
    endforeach()

    copy_sentry_cocoa_type_headers("${sentry_cocoa_slice_build_dir}/SentryObjC.framework")

    list(APPEND sentry_cocoa_create_xcframework_args
        -framework "${sentry_cocoa_slice_build_dir}/SentryObjC.framework"
    )
    list(APPEND sentry_cocoa_built_slice_names "${sentry_cocoa_slice}")
    list(APPEND sentry_cocoa_built_slice_dirs "${sentry_cocoa_slice_build_dir}")
endforeach()

run_sentry_cocoa_command(
    "${SENTRY_COCOA_XCODEBUILD}" -create-xcframework
    ${sentry_cocoa_create_xcframework_args}
    -output "${SENTRY_COCOA_XCFRAMEWORK}"
)

list(LENGTH sentry_cocoa_built_slice_names sentry_cocoa_built_slice_count)
math(EXPR sentry_cocoa_built_slice_last_index "${sentry_cocoa_built_slice_count} - 1")
foreach(sentry_cocoa_built_slice_index RANGE ${sentry_cocoa_built_slice_last_index})
    list(GET sentry_cocoa_built_slice_names
        ${sentry_cocoa_built_slice_index}
        sentry_cocoa_slice
    )
    list(GET sentry_cocoa_built_slice_dirs
        ${sentry_cocoa_built_slice_index}
        sentry_cocoa_slice_build_dir
    )
    set(sentry_cocoa_dependency_framework_dir
        "${SENTRY_COCOA_XCFRAMEWORK}/${sentry_cocoa_slice}/Dependencies"
    )
    file(MAKE_DIRECTORY "${sentry_cocoa_dependency_framework_dir}")
    foreach(sentry_cocoa_framework_name IN LISTS sentry_cocoa_dependency_frameworks)
        file(COPY
            "${sentry_cocoa_slice_build_dir}/${sentry_cocoa_framework_name}.framework"
            DESTINATION "${sentry_cocoa_dependency_framework_dir}"
        )
    endforeach()
endforeach()

foreach(sentry_cocoa_required_output IN LISTS sentry_cocoa_required_outputs)
    if(NOT EXISTS "${sentry_cocoa_required_output}")
        message(FATAL_ERROR "SentryObjC build did not create '${sentry_cocoa_required_output}'.")
    endif()
    if(sentry_cocoa_required_output MATCHES "/Headers/")
        continue()
    endif()
    run_sentry_cocoa_command(
        "${SENTRY_COCOA_LIPO}" -info "${sentry_cocoa_required_output}"
    )
endforeach()

file(WRITE "${SENTRY_COCOA_STAMP_FILE}" "${sentry_cocoa_expected_stamp}")
