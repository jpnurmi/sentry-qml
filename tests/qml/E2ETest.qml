import QtQml
import Sentry 1.0

QtObject {
    property bool initialized: false
    property bool flushed: false
    property bool closed: false
    property string eventId: ""
    property string message: "Sentry QML E2E " + testRunId
    property SentryOptions options: SentryOptions {
        dsn: testDsn
        databasePath: testDatabasePath
        release: "sentry-qml@e2e"
        environment: "ci"
        shutdownTimeout: 5000
    }

    Component.onCompleted: {
        initialized = Sentry.init(options)
        Sentry.setTag("e2e_run_id", testRunId)
        eventId = Sentry.captureMessage(message, "info")
        flushed = Sentry.flush(10000)
        closed = Sentry.close()
    }
}
