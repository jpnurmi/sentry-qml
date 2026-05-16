import QtQuick
import Sentry 1.0

Window {
    objectName: "screenshotWindow"
    width: 160
    height: 120
    visible: true
    color: "#224466"

    property bool initialized: false
    property bool flushed: false
    property bool closed: false
    property string eventId: ""
    property SentryOptions options: SentryOptions {
        dsn: testDsn
        databasePath: testDatabasePath
        autoSessionTracking: false
        attachScreenshot: true
        shutdownTimeout: 2000
    }

    Rectangle {
        objectName: "screenshotMarker"
        anchors.centerIn: parent
        width: 48
        height: 32
        color: "#ff6655"
    }

    Timer {
        interval: 50
        running: initialized
        repeat: false
        onTriggered: {
            eventId = Sentry.captureMessage("Screenshot event")
            flushed = Sentry.flush(2000)
            closed = Sentry.close()
        }
    }

    Component.onCompleted: {
        initialized = Sentry.init(options)
    }
}
