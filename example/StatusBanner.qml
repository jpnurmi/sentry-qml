import QtQuick
import QtQuick.Controls
import Sentry 1.0

import "controls"

Popup {
    id: banner

    function isInitializeStatus(message) {
        return message === qsTr("Initialization failed") || message === qsTr("Re-initialization failed");
    }

    function reset() {
        if (!isInitializeStatus(AppState.statusMessage))
            AppState.setStatus(qsTr("Ready"), Sentry.initialized, Sentry.initialized ? 1 : 0);
    }

    function statusText() {
        if (isInitializeStatus(AppState.statusMessage))
            return AppState.statusMessage;
        if (AppState.statusMessage !== qsTr("Ready"))
            return AppState.statusMessage;
        return Sentry.initialized ? qsTr("Ready") : qsTr("Not initialized");
    }

    function statusSeverity() {
        if (isInitializeStatus(AppState.statusMessage))
            return 2;
        if (AppState.statusMessage !== qsTr("Ready"))
            return AppState.statusSeverity;
        return Sentry.initialized ? 1 : 0;
    }

    modal: false
    dim: false
    focus: false
    closePolicy: Popup.NoAutoClose
    padding: 0

    contentItem: Pill {
        severity: banner.statusSeverity()
        text: banner.statusText()
    }

    background: Item { }
}
