import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Sentry 1.0

ApplicationWindow {
    id: window

    width: 560
    height: 360
    minimumWidth: 420
    minimumHeight: 320
    visible: true
    title: qsTr("Sentry QML")

    readonly property string sentryDatabasePath: StandardPaths.writableLocation(StandardPaths.CacheLocation) + "/sentry"

    Settings {
        category: "sentry"
        property alias dsn: dsnField.text
        property alias message: messageField.text
        property alias release: releaseField.text
        property alias environment: environmentField.text
        property alias debug: debugCheckBox.checked
    }

    SentryOptions {
        id: sentryOptions

        dsn: dsnField.text
        databasePath: window.sentryDatabasePath
        release: releaseField.text
        environment: environmentField.text
        debug: debugCheckBox.checked
        beforeSend: function(event) {
            console.log("### beforeSend");
            event.extra = event.extra || {}
            event.extra.example = "sentry-qml"
            return event
        }
        onCrash: function(event) {
            console.log("### onCrash");
            //console.log(JSON.stringify(event));
            event.extra = event.extra || {}
            event.extra.exampleCrash = true
            return event
        }
    }

    function initializeSentry() {
        const ok = Sentry.init(sentryOptions)

        statusLabel.text = ok ? qsTr("Sentry initialized") : qsTr("Initialization failed")
        return ok
    }

    function triggerQmlError() {
        callMissingQmlFunction()
    }

    function callMissingQmlFunction() {
        missingQmlFunction()
    }

    Connections {
        target: Sentry

        function onErrorOccurred(message) {
            statusLabel.text = message
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        Label {
            text: qsTr("Sentry QML")
            font.pixelSize: 24
            font.weight: Font.DemiBold
            Layout.fillWidth: true
        }

        TextField {
            id: dsnField

            placeholderText: qsTr("DSN")
            Layout.fillWidth: true
        }

        TextField {
            id: messageField

            text: qsTr("Hello from Sentry QML")
            placeholderText: qsTr("Message")
            Layout.fillWidth: true
        }

        RowLayout {
            spacing: 10
            Layout.fillWidth: true

            TextField {
                id: releaseField

                text: qsTr("sentry-qml-example@0.1.0")
                placeholderText: qsTr("Release")
                Layout.fillWidth: true
            }

            TextField {
                id: environmentField

                text: qsTr("qml")
                placeholderText: qsTr("Environment")
                Layout.fillWidth: true
            }
        }

        CheckBox {
            id: debugCheckBox

            text: qsTr("Debug logging")
        }

        RowLayout {
            spacing: 10
            Layout.fillWidth: true

            Button {
                text: Sentry.initialized ? qsTr("Initialized") : qsTr("Initialize")
                enabled: !Sentry.initialized

                onClicked: {
                    initializeSentry()
                }
            }

            Button {
                text: qsTr("Add Breadcrumb")
                enabled: Sentry.initialized

                onClicked: {
                    const ok = Sentry.addBreadcrumb({
                        message: "Example breadcrumb",
                        category: "example",
                        type: "manual",
                        level: "info",
                        data: { message: messageField.text }
                    })
                    statusLabel.text = ok
                        ? qsTr("Breadcrumb added")
                        : qsTr("Breadcrumb was not added")
                }
            }

            Button {
                text: qsTr("Set Tag")
                enabled: Sentry.initialized

                onClicked: {
                    const ok = Sentry.setTag("example.environment", environmentField.text)
                    statusLabel.text = ok
                        ? qsTr("Tag set")
                        : qsTr("Tag was not set")
                }
            }

            Button {
                text: qsTr("Capture Message")
                enabled: Sentry.initialized

                onClicked: {
                    const eventId = Sentry.captureMessage(messageField.text, "info")
                    statusLabel.text = eventId.length > 0
                        ? qsTr("Captured event %1").arg(eventId)
                        : qsTr("Message was not captured")
                }
            }

            Button {
                text: qsTr("Capture Exception")
                enabled: Sentry.initialized

                onClicked: {
                    try {
                        throw new Error("Caught QML exception")
                    } catch (exception) {
                        const eventId = Sentry.captureException(exception)
                        statusLabel.text = eventId.length > 0
                            ? qsTr("Captured exception %1").arg(eventId)
                            : qsTr("Exception was not captured")
                    }
                }
            }

            Item {
                Layout.fillWidth: true
            }
        }

        RowLayout {
            spacing: 10
            Layout.fillWidth: true

            Button {
                text: qsTr("Trigger QML Error")
                enabled: Sentry.initialized

                onClicked: {
                    statusLabel.text = qsTr("Triggering QML error...")
                    triggerQmlError()
                }
            }

            Button {
                text: qsTr("Send Log")
                enabled: Sentry.initialized

                onClicked: {
                    const ok = Sentry.info("Example structured log", {
                        "example.message": messageField.text
                    })
                    statusLabel.text = ok
                        ? qsTr("Log queued")
                        : qsTr("Log was not queued")
                }
            }

            Button {
                text: qsTr("Record Metric")
                enabled: Sentry.initialized

                onClicked: {
                    const ok = Sentry.distribution("example.message.length",
                                                   messageField.text.length,
                                                   "character",
                                                   { "example.source": "qml" })
                    statusLabel.text = ok
                        ? qsTr("Metric queued")
                        : qsTr("Metric was not queued")
                }
            }

            Button {
                text: qsTr("Crash")
                enabled: Sentry.initialized

                onClicked: {
                    statusLabel.text = qsTr("Crashing...")
                    exampleActions.crash()
                }
            }

            Item {
                Layout.fillWidth: true
            }
        }

        Label {
            id: statusLabel

            text: qsTr("Not initialized")
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}
