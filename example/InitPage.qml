import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import "controls"

Item {
    id: root

    property bool compact: false
    property bool initialized: false
    property real pageMargin: AppTheme.pageMargin
    property real panelMargin: AppTheme.panelMargin

    signal initializeRequested

    function openDatabaseFolderDialog() {
        databaseFolderDialog.currentFolder = AppState.toFileUrl(AppState.databasePath);
        databaseFolderDialog.open();
    }

    ScrollView {
        id: initializeScrollView

        anchors.fill: parent
        clip: true
        padding: root.pageMargin
        contentWidth: availableWidth

        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical.policy: ScrollBar.AsNeeded

        ColumnLayout {
            id: initializePage

            width: initializeScrollView.availableWidth
            spacing: AppTheme.pageSpacing

            PageHeader {
                id: initializeHeader

                compact: root.compact
                canGoBack: false
            }

            Rectangle {
                id: initializePanel

                Layout.fillWidth: true
                Layout.bottomMargin: root.pageMargin
                implicitHeight: setupLayout.implicitHeight + root.panelMargin
                color: AppTheme.surface
                radius: 8

                ColumnLayout {
                    id: setupLayout

                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: root.panelMargin
                    anchors.topMargin: 0
                    spacing: AppTheme.groupSpacing

                    Label {
                        id: initializeTitle

                        text: qsTr("OPTIONS")
                        color: AppTheme.text
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                        verticalAlignment: Text.AlignVCenter
                        Layout.fillWidth: true
                        Layout.preferredHeight: 32
                    }

                    LabeledTextField {
                        label: qsTr("DSN")
                        text: AppState.dsn
                        placeholderText: qsTr("https://public@example.ingest.sentry.io/1")
                        Layout.fillWidth: true
                        onTextEdited: AppState.dsn = text
                    }

                    LabeledTextField {
                        Layout.fillWidth: true
                        label: qsTr("Database")
                        text: AppState.databasePath
                        placeholderText: qsTr("/path/to/sentry-db")
                        trailingActionText: "\u2026"
                        trailingActionAccessibleName: qsTr("Browse database")
                        trailingActionTooltip: qsTr("Browse...")
                        onTextEdited: AppState.databasePath = text
                        onTrailingActionTriggered: root.openDatabaseFolderDialog()
                    }

                    FolderDialog {
                        id: databaseFolderDialog

                        title: qsTr("Choose database")
                        currentFolder: AppState.toFileUrl(AppState.databasePath)
                        onAccepted: {
                            AppState.databasePath = AppState.toLocalPath(selectedFolder);
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: root.compact ? 1 : 3
                        uniformCellWidths: true
                        rowSpacing: AppTheme.formSpacing
                        columnSpacing: AppTheme.formSpacing

                        LabeledTextField {
                            label: qsTr("Release")
                            text: AppState.release
                            placeholderText: qsTr("my-app@1.0.0")
                            Layout.fillWidth: true
                            onTextEdited: AppState.release = text
                        }

                        LabeledTextField {
                            label: qsTr("Environment")
                            text: AppState.environment
                            placeholderText: qsTr("production")
                            Layout.fillWidth: true
                            onTextEdited: AppState.environment = text
                        }

                        LabeledTextField {
                            label: qsTr("Distribution")
                            text: AppState.dist
                            placeholderText: qsTr("1")
                            Layout.fillWidth: true
                            onTextEdited: AppState.dist = text
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: root.compact ? 1 : 3
                        uniformCellWidths: true
                        rowSpacing: AppTheme.formSpacing
                        columnSpacing: AppTheme.formSpacing

                        LabeledDoubleSpinBox {
                            label: qsTr("Sample rate")
                            value: AppState.sampleRate
                            from: 0.0
                            to: 1.0
                            stepSize: 0.1
                            decimals: 1
                            locale: Qt.locale("en_US")
                            Layout.fillWidth: true
                            onValueModified: AppState.sampleRate = value
                        }

                        LabeledSpinBox {
                            label: qsTr("Max breadcrumbs")
                            value: AppState.maxBreadcrumbs
                            from: 0
                            to: 1000
                            stepSize: 1
                            Layout.fillWidth: true
                            onValueModified: AppState.maxBreadcrumbs = value
                        }

                        LabeledSpinBox {
                            label: qsTr("Shutdown timeout (ms)")
                            value: AppState.shutdownTimeout
                            from: 0
                            to: 60000
                            stepSize: 100
                            Layout.fillWidth: true
                            onValueModified: AppState.shutdownTimeout = value
                        }
                    }

                    GridLayout {
                        id: featureFlow

                        Layout.fillWidth: true
                        columns: root.compact ? 1 : 4
                        flow: GridLayout.LeftToRight
                        uniformCellWidths: !root.compact
                        rowSpacing: AppTheme.formSpacing
                        columnSpacing: AppTheme.pageSpacing

                        CheckBox {
                            text: qsTr("Debug")
                            checked: AppState.debugEnabled
                            Layout.fillWidth: true
                            onToggled: AppState.debugEnabled = checked
                        }

                        CheckBox {
                            text: qsTr("Logs")
                            checked: AppState.logsEnabled
                            Layout.fillWidth: true
                            onToggled: AppState.logsEnabled = checked
                        }

                        CheckBox {
                            text: qsTr("Metrics")
                            checked: AppState.metricsEnabled
                            Layout.fillWidth: true
                            onToggled: AppState.metricsEnabled = checked
                        }

                        CheckBox {
                            text: qsTr("Auto sessions")
                            checked: AppState.autoSessionTrackingEnabled
                            Layout.fillWidth: true
                            onToggled: AppState.autoSessionTrackingEnabled = checked
                        }

                        CheckBox {
                            text: qsTr("Require consent")
                            checked: AppState.requireUserConsentEnabled
                            Layout.fillWidth: true
                            onToggled: AppState.requireUserConsentEnabled = checked
                        }

                        CheckBox {
                            text: qsTr("View hierarchy")
                            checked: AppState.viewHierarchyEnabled
                            Layout.fillWidth: true
                            onToggled: AppState.viewHierarchyEnabled = checked
                        }
                    }
                }
            }
        }
    }

    FloatingActionButton {
        readonly property string actionText: root.initialized ? qsTr("Re-initialize") : qsTr("Initialize")

        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: root.pageMargin
        implicitWidth: 56
        implicitHeight: 56
        text: root.initialized ? "\u21bb" : "\u2192"
        tooltip: actionText
        font.pixelSize: root.initialized ? 24 : 28
        font.weight: Font.DemiBold
        onClicked: root.initializeRequested()
    }
}
