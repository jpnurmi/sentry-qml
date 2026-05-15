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

    StatusBanner {
        id: statusBanner

        x: Math.max(AppTheme.pageMargin, window.width - width - AppTheme.pageMargin)
        y: AppTheme.pageMargin
    }

    Component {
        id: runtimePage

        RuntimePage {
            nativeCrashAction: function() {
                exampleActions.crash();
            }

            onBackRequested: {
                statusBanner.reset();
                stackView.pop();
            }
        }
    }
}
