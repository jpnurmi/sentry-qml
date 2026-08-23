set(sentry_qml_static_default OFF)
if(EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(sentry_qml_static_default ON)
endif()
option(SENTRY_QML_STATIC_LIBRARY "Build Sentry QML as a static library"
    ${sentry_qml_static_default}
)

if(SENTRY_QML_STATIC_LIBRARY)
    qt_add_library(SentryQml STATIC)
else()
    qt_add_library(SentryQml SHARED)
endif()

qt_add_qml_module(SentryQml
    URI ${SENTRY_QML_IMPORT_URI}
    VERSION ${SENTRY_QML_IMPORT_VERSION}
    OUTPUT_DIRECTORY "${SENTRY_QML_OUTPUT_DIRECTORY}/${SENTRY_QML_IMPORT_URI}"
    OUTPUT_TARGETS sentry_qml_module_targets
    SOURCES
        ../include/SentryQml/sentryattachment.h
        ../include/SentryQml/sentry.h
        ../include/SentryQml/sentryhint.h
        ../include/SentryQml/sentryintegration.h
        ../include/SentryQml/sentryintegrationcontext.h
        ../include/SentryQml/sentryintegrationplugin.h
        ../include/SentryQml/sentryoptions.h
        ../include/SentryQml/sentryqmlglobal.h
        ../include/SentryQml/sentryspan.h
        ../include/SentryQml/sentryuser.h
        ../include/SentryQml/private/sentryevent_p.h
        ../include/SentryQml/private/sentryhint_p.h
        ../include/SentryQml/private/sentryintegrationmanager_p.h
        ../include/SentryQml/private/sentryqmlengine_p.h
        ../include/SentryQml/private/sentrysdk_p.h
        ../include/SentryQml/private/sentryscreenshot_p.h
        ../include/SentryQml/private/sentryviewhierarchy_p.h
        ${sentry_qml_backend_sources}
        sentry.cpp
        sentryattachment.cpp
        sentryevent.cpp
        sentryhint.cpp
        sentryintegration.cpp
        sentryintegrationmanager.cpp
        sentryoptions.cpp
        sentryqmlengine.cpp
        sentryspan.cpp
        sentryscreenshot.cpp
        sentryuser.cpp
        sentryviewhierarchy.cpp
)

if(sentry_qml_backend_dependencies)
    add_dependencies(SentryQml ${sentry_qml_backend_dependencies})
endif()

get_cmake_property(SENTRY_QML_MULTI_CONFIG GENERATOR_IS_MULTI_CONFIG)
if(SENTRY_QML_MULTI_CONFIG AND TARGET SentryQmlplugin)
    set(sentry_qml_module_output_dir "${SENTRY_QML_OUTPUT_DIRECTORY}/${SENTRY_QML_IMPORT_URI}")
    add_custom_command(TARGET SentryQmlplugin POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${sentry_qml_module_output_dir}"
            "${sentry_qml_module_output_dir}/$<CONFIG>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:SentryQmlplugin>"
            "${sentry_qml_module_output_dir}/$<TARGET_FILE_NAME:SentryQmlplugin>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:SentryQmlplugin>"
            "${sentry_qml_module_output_dir}/$<CONFIG>/$<TARGET_FILE_NAME:SentryQmlplugin>"
        VERBATIM
    )
endif()

target_include_directories(SentryQml
    PUBLIC
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    PRIVATE
        ${sentry_qml_backend_include_directories}
        "${PROJECT_SOURCE_DIR}/include/SentryQml"
)

target_compile_definitions(SentryQml
    PRIVATE
        SENTRYQML_LIBRARY
        SENTRY_QML_VERSION="${PROJECT_VERSION}"
        SENTRY_QML_SDK_NAME="${SENTRY_SDK_NAME}"
        ${sentry_qml_backend_compile_definitions}
)

target_compile_definitions(SentryQml
    PUBLIC
        SENTRY_QML_SDK_${SENTRY_QML_SDK}
)
if(SENTRY_QML_BACKEND)
    target_compile_definitions(SentryQml
        PUBLIC
            SENTRY_QML_BACKEND_${SENTRY_QML_BACKEND}
    )
endif()

if(sentry_qml_backend_link_options)
    target_link_options(SentryQml
        INTERFACE
            ${sentry_qml_backend_link_options}
    )
endif()

if(SENTRY_QML_STATIC_LIBRARY)
    target_compile_definitions(SentryQml
        PUBLIC
            SENTRYQML_STATIC
    )
endif()

set(SENTRY_QML_HAS_QML_PRIVATE OFF)
if(TARGET Qt6::QmlPrivate)
    set(SENTRY_QML_HAS_QML_PRIVATE ON)
    target_compile_definitions(SentryQml
        PRIVATE
            SENTRY_QML_HAS_QML_PRIVATE
    )
    target_link_libraries(SentryQml
        PRIVATE
            Qt6::QmlPrivate
    )
endif()

if(WIN32)
    target_compile_definitions(SentryQml
        PRIVATE
            NOMINMAX
            WIN32_LEAN_AND_MEAN
    )
endif()

target_link_libraries(SentryQml
    PUBLIC
        Qt6::Core
        Qt6::Qml
    PRIVATE
        Qt6::Gui
        Qt6::Quick
        ${sentry_qml_backend_libraries}
)

set_target_properties(SentryQml PROPERTIES
    OUTPUT_NAME SentryQml
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/SentryQml"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    PATTERN "private" EXCLUDE
)

set(sentry_qml_install_targets SentryQml)
if(SENTRY_QML_STATIC_LIBRARY)
    list(APPEND sentry_qml_install_targets ${sentry_qml_module_targets})
endif()

install(TARGETS ${sentry_qml_install_targets}
    EXPORT SentryQmlTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    OBJECTS DESTINATION ${CMAKE_INSTALL_LIBDIR}/sentry-qml/objects
)

install(EXPORT SentryQmlTargets
    NAMESPACE SentryQml::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SentryQml
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/SentryQmlConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/cmake/SentryQmlConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/SentryQmlConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SentryQml
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/SentryQmlConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/SentryQmlConfigVersion.cmake"
    "${PROJECT_SOURCE_DIR}/cmake/SentryQmlIntegration.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SentryQml
)
