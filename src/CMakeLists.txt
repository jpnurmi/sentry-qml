qt_add_library(SentryQml SHARED)

qt_add_qml_module(SentryQml
    URI ${SENTRY_QML_IMPORT_URI}
    VERSION ${SENTRY_QML_IMPORT_VERSION}
    OUTPUT_DIRECTORY "${SENTRY_QML_OUTPUT_DIRECTORY}/${SENTRY_QML_IMPORT_URI}"
    SOURCES
        ../include/SentryQml/sentry.h
        ../include/SentryQml/sentryoptions.h
        ../include/SentryQml/sentryqmlglobal.h
        ../include/SentryQml/private/sentryevent_p.h
        ../include/SentryQml/private/sentrynativesdk_p.h
        ../include/SentryQml/private/sentryqmlengine_p.h
        sentry.cpp
        sentryevent.cpp
        sentrynativesdk.cpp
        sentryoptions.cpp
        sentryqmlengine.cpp
)

target_include_directories(SentryQml
    PUBLIC
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    PRIVATE
        "${SENTRY_NATIVE_DIR}"
        "${PROJECT_SOURCE_DIR}/include/SentryQml"
)

target_compile_definitions(SentryQml
    PRIVATE
        SENTRYQML_LIBRARY
        SENTRY_QML_SDK_NAME="${SENTRY_SDK_NAME}"
)

target_link_libraries(SentryQml
    PUBLIC
        Qt6::Core
        Qt6::Qml
    PRIVATE
        Qt6::QmlPrivate
        sentry::sentry
)

set_target_properties(SentryQml PROPERTIES
    OUTPUT_NAME SentryQml
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(TARGETS SentryQml
    EXPORT SentryQmlTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT SentryQmlTargets
    NAMESPACE SentryQml::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SentryQml
)
