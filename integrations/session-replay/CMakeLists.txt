sentry_qml_add_integration(
    TARGET SentryQmlSessionReplayIntegration
    ID session-replay
    CLASS_NAME SentryQmlSessionReplayIntegration
    METADATA "${CMAKE_CURRENT_SOURCE_DIR}/sessionreplayintegration.json"
    SOURCES
        replaycapture.cpp
        replaycapture.h
        replayconfiguration.cpp
        replayconfiguration.h
        replayencoder.cpp
        replayencoder.h
        replayprivacy.cpp
        replayprivacy.h
        replayspool.cpp
        replayspool.h
        sessionreplayintegration.cpp
        sessionreplayintegration.h
        sessionreplayintegration.json
    LIBRARIES
        Qt6::Gui
        Qt6::Quick
        Qt6::Multimedia
)

if(WIN32)
    target_compile_definitions(SentryQmlSessionReplayIntegration PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

if(BUILD_TESTING AND TARGET Qt6::Test)
    add_executable(tst_session_replay
        replayconfiguration.cpp
        replayconfiguration.h
        replayencoder.cpp
        replayencoder.h
        replayprivacy.cpp
        replayprivacy.h
        replayspool.cpp
        replayspool.h
        tst_sessionreplay.cpp
    )
    target_link_libraries(tst_session_replay PRIVATE
        SentryQml
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        Qt6::Multimedia
        Qt6::Test
    )
    add_test(NAME tst_session_replay COMMAND tst_session_replay -platform offscreen)
endif()
