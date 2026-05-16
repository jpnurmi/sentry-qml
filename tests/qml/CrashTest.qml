import QtQuick
import Sentry 1.0

Window {
    width: 160
    height: 120
    visible: testAttachScreenshot
    color: "#334455"

    property SentryOptions options: SentryOptions {
        dsn: testDsn
        databasePath: testDatabasePath
        release: "sentry-qml@crash"
        environment: "crash-test"
        dist: "7"
        autoSessionTracking: false
        attachScreenshot: testAttachScreenshot
        maxBreadcrumbs: 10
        shutdownTimeout: 5000
        user: SentryUser {
            userId: "crash-user"
            username: "crash"
            email: "crash@example.com"
        }
    }

    Rectangle {
        anchors.centerIn: parent
        width: 48
        height: 32
        color: "#55cc88"
    }

    Timer {
        interval: 50
        running: testAttachScreenshot
        repeat: false
        onTriggered: crashActions.crash(testCrashType)
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
        if (!testAttachScreenshot)
            crashActions.crash(testCrashType)
    }
}
