import QtQml
import Sentry 1.0

QtObject {
    property SentryOptions options: SentryOptions {
        dsn: testDsn
        databasePath: testDatabasePath
        release: "sentry-qml@crash"
        environment: "crash-test"
        dist: "7"
        autoSessionTracking: false
        maxBreadcrumbs: 10
        shutdownTimeout: 5000
        user: SentryUser {
            userId: "crash-user"
            username: "crash"
            email: "crash@example.com"
        }
    }

    Component.onCompleted: {
        Sentry.init(options)
        Sentry.setTag("scenario", "crash")
        Sentry.setTag("crash_type", testCrashType)
        Sentry.setContext("qml", {
            scenario: "crash",
            crashType: testCrashType,
            phase: "helper"
        })
        Sentry.setFingerprint(["{{ default }}", "qml-crash"])
        Sentry.addBreadcrumb({
            message: "crash breadcrumb",
            category: "test",
            type: "default",
            level: "fatal",
            data: { scenario: "crash" }
        })
        crashActions.crash(testCrashType)
    }
}
