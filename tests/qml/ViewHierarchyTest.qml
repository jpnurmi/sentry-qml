import QtQuick
import Sentry 1.0

Window {
    objectName: "viewHierarchyWindow"
    width: 320
    height: 240

    property bool initialized: false
    property bool flushed: false
    property bool closed: false
    property string eventId: ""
    property SentryOptions options: SentryOptions {
        dsn: testDsn
        databasePath: testDatabasePath
        autoSessionTracking: false
        attachViewHierarchy: true
        shutdownTimeout: 2000
    }

    Rectangle {
        objectName: "viewHierarchyChild"
        x: 12
        y: 34
        width: 56
        height: 78
        opacity: 0.5
    }

    Component.onCompleted: {
        initialized = Sentry.init(options)
        eventId = Sentry.captureMessage("View hierarchy event")
        flushed = Sentry.flush(2000)
        closed = Sentry.close()
    }
}
