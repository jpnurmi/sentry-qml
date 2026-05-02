import QtQml
import Sentry 1.0

QtObject {
    property bool initialized: false
    property bool flushed: false
    property bool closed: false
    property bool success: false
    property string eventId: ""
    property string message: "Sentry QML E2E " + testRunId
    property SentryOptions options: SentryOptions {
        dsn: testDsn
        databasePath: testDatabasePath
        release: "sentry-qml@e2e"
        environment: "ci"
        shutdownTimeout: 5000
        autoSessionTracking: false
        user: SentryUser {
            userId: "e2e-user"
            username: "e2e"
            email: "e2e@example.com"
        }
    }

    Component.onCompleted: {
        initialized = Sentry.init(options)
        Sentry.setTag("e2e_run_id", testRunId)
        Sentry.setTag("test.suite", "e2e")
        Sentry.setTag("test.action", testAction)

        if (testAction === "message-capture") {
            eventId = Sentry.captureMessage(message, "info")
            flushed = Sentry.flush(10000)
            closed = Sentry.close()
            success = initialized && eventId !== "" && flushed && closed
        } else if (testAction === "crash-send") {
            flushed = Sentry.flush(10000)
            closed = Sentry.close()
            success = initialized && flushed && closed
        } else if (testAction === "crash-capture") {
            Sentry.setTag("test.crash_id", testCrashId)
            Sentry.setTag("crash_type", "segfault")
            Sentry.setContext("qml", {
                scenario: "e2e-crash",
                crashId: testCrashId
            })
            Sentry.setFingerprint(["{{ default }}", "qml-e2e-crash"])
            Sentry.addBreadcrumb({
                message: "E2E crash breadcrumb",
                category: "e2e",
                type: "default",
                level: "fatal",
                data: { crashId: testCrashId }
            })
            crashActions.crash()
        }
    }
}
