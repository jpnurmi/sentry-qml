cmake_minimum_required(VERSION 3.21)

project(SentryQmlMinimalIntegration LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core)
find_package(SentryQml CONFIG REQUIRED)

qt_standard_project_setup(REQUIRES 6.8)

sentry_qml_add_integration(
    TARGET SentryQmlMinimalIntegration
    ID minimal
    CLASS_NAME MinimalIntegration
    METADATA "${CMAKE_CURRENT_SOURCE_DIR}/minimalintegration.json"
    SOURCES
        minimalintegration.cpp
        minimalintegration.json
)
