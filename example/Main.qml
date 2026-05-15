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

    function resetStatus() {
        if (!isInitializeStatus(AppState.statusMessage))
            AppState.setStatus(qsTr("Ready"), Sentry.initialized, Sentry.initialized ? 1 : 0);
    }

    function getStatusText() {
        if (isInitializeStatus(AppState.statusMessage))
            return AppState.statusMessage;
        if (AppState.statusMessage !== qsTr("Ready"))
            return AppState.statusMessage;
        return Sentry.initialized ? qsTr("Ready") : qsTr("Not initialized");
    }

    function getStatusSeverity() {
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
        id: stackView

        anchors.fill: parent
        initialItem: InitPage {
            onInitialized: {
                AppState.setStatus(qsTr("Ready"), true);
                if (stackView.depth === 1)
                    stackView.push(runtimePage);
                AppState.sessionActive = false;
            }
            onFailed: {
                AppState.setStatus(qsTr("Re-initialization failed"), false);
            }
        }
    }

    Popup {
        id: statusPopup

        modal: false
        dim: false
        focus: false
        closePolicy: Popup.NoAutoClose
        padding: 0
        visible: true
        x: Math.max(AppTheme.pageMargin, window.width - width - AppTheme.pageMargin)
        y: AppTheme.pageMargin
        background: Item {}
        contentItem: Banner {
            severity: getStatusSeverity()
            text: getStatusText()
        }
    }

    Component {
        id: runtimePage

        RuntimePage {
            nativeCrashAction: function() {
                exampleActions.crash();
            }

            onBackRequested: {
                resetStatus();
                stackView.pop();
            }
        }
    }
}
