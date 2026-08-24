cmake_minimum_required(VERSION 3.21)

project(SentryQmlInstalledConsumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core)
find_package(SentryQml CONFIG REQUIRED)

qt_standard_project_setup(REQUIRES 6.8)

sentry_qml_add_integration(
    TARGET InstalledSmokeIntegration
    ID installed-smoke
    CLASS_NAME InstalledSmokeIntegration
    METADATA "${CMAKE_CURRENT_SOURCE_DIR}/installedsmokeintegration.json"
    SOURCES
        installedsmokeintegration.cpp
        installedsmokeintegration.json
)

qt_add_executable(sentry_qml_installed_consumer main.cpp)
set_target_properties(sentry_qml_installed_consumer PROPERTIES
    QT_QML_MODULE_NO_IMPORT_SCAN TRUE
)
target_link_libraries(sentry_qml_installed_consumer PRIVATE SentryQml::SentryQml)
sentry_qml_deploy_integrations(
    TARGET sentry_qml_installed_consumer
    INTEGRATIONS InstalledSmokeIntegration
)
add_dependencies(sentry_qml_installed_consumer InstalledSmokeIntegration)
