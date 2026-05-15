import QtCore
import QtQuick
import QtQuick.Controls
import Sentry 1.0

import "controls"

ApplicationWindow {
    id: window

    width: 1040
    height: 720
    minimumWidth: 320
    minimumHeight: 480
    visible: true
    title: qsTr("Sentry QML")
    color: AppTheme.background

    Binding {
        target: AppTheme
        property: "compact"
        value: window.width < AppTheme.compactWidth
    }

    function isInitializeStatus(message) {
        return message === qsTr("Initialization failed") || message === qsTr("Re-initialization failed");
    }

    function resetRuntimeStatus() {
        if (!isInitializeStatus(AppState.statusMessage))
            AppState.setStatus(qsTr("Ready"), Sentry.initialized, Sentry.initialized ? 1 : 0);
    }

    function globalStatusText() {
        if (isInitializeStatus(AppState.statusMessage))
            return AppState.statusMessage;
        if (AppState.statusMessage !== qsTr("Ready"))
            return AppState.statusMessage;
        return Sentry.initialized ? qsTr("Ready") : qsTr("Not initialized");
    }

    function globalStatusSeverity() {
        if (isInitializeStatus(AppState.statusMessage))
            return 2;
        if (AppState.statusMessage !== qsTr("Ready"))
            return AppState.statusSeverity;
        return Sentry.initialized ? 1 : 0;
    }

    Connections {
        target: Sentry

        function onErrorOccurred(message) {
            AppState.setStatus(message, false);
        }
    }

    StackView {
        id: pageStack

        anchors.fill: parent
        initialItem: InitPage {
            onInitialized: {
                AppState.setStatus(qsTr("Ready"), true);
                if (pageStack.depth === 1)
                    pageStack.push(runtimePageComponent);
                AppState.sessionActive = false;
            }
            onFailed: {
                AppState.setStatus(qsTr("Re-initialization failed"), false);
            }
        }
    }

    Popup {
        id: globalStatusPopup

        modal: false
        dim: false
        focus: false
        closePolicy: Popup.NoAutoClose
        padding: 0
        visible: true
        width: globalStatusPill.implicitWidth
        height: globalStatusPill.implicitHeight
        x: Math.max(AppTheme.pageMargin, window.width - width - AppTheme.pageMargin)
        y: AppTheme.pageMargin
        background: Item {}
        contentItem: Banner {
            id: globalStatusPill

            severity: globalStatusSeverity()
            text: globalStatusText()
        }
    }

    Component {
        id: runtimePageComponent

        RuntimePage {
            nativeCrashAction: function() {
                exampleActions.crash();
            }

            onBackRequested: {
                resetRuntimeStatus();
                pageStack.pop();
            }
        }
    }
}
