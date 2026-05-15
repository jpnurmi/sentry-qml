import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Sentry 1.0

import "controls"

Item {
    id: page

    signal failed
    signal initialized

    function initialize() {
        if (Sentry.initialized && !Sentry.close()) {
            page.failed();
            return false;
        }

        if (!Sentry.init(options)) {
            page.failed();
            return false;
        }

        page.initialized()
        return true;
    }

    SentryOptions {
        id: options

        dsn: AppState.dsn
        databasePath: AppState.databasePath
        release: AppState.release
        environment: AppState.environment
        dist: AppState.dist
        debug: AppState.debugEnabled
        enableLogs: AppState.logsEnabled
        enableMetrics: AppState.metricsEnabled
        autoSessionTracking: AppState.autoSessionTrackingEnabled
        requireUserConsent: AppState.requireUserConsentEnabled
        attachViewHierarchy: AppState.viewHierarchyEnabled
        sampleRate: AppState.sampleRate
        maxBreadcrumbs: AppState.maxBreadcrumbs
        shutdownTimeout: AppState.shutdownTimeout
        user: SentryUser {
            userId: AppState.userId
            username: AppState.username
            email: AppState.email
            ipAddress: AppState.ipAddress
        }
        beforeSend: function (event) {
            console.log("### beforeSend");
            event.extra = event.extra || {};
            event.extra.example = "sentry-qml";
            return event;
        }
        onCrash: function (event) {
            console.log("### onCrash");
            event.extra = event.extra || {};
            event.extra.exampleCrash = true;
            return event;
        }
    }

    ScrollView {
        id: scrollView

        anchors.fill: parent
        clip: true
        padding: AppTheme.pageMargin
        contentWidth: availableWidth

        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical.policy: ScrollBar.AsNeeded

        ColumnLayout {
            width: scrollView.availableWidth
            spacing: AppTheme.pageSpacing

            PageHeader {
                canGoBack: false
            }

            Panel {
                Layout.fillWidth: true
                Layout.bottomMargin: AppTheme.pageMargin
                title: qsTr("Options")
                spacing: AppTheme.groupSpacing

                ColumnLayout {
                    id: setupLayout

                    anchors.fill: parent
                    spacing: AppTheme.groupSpacing

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
                        onTrailingActionTriggered: folderDialog.open()
                    }

                    FolderDialog {
                        id: folderDialog

                        title: qsTr("Choose database")
                        currentFolder: AppState.toFileUrl(AppState.databasePath)
                        onAccepted: {
                            AppState.databasePath = AppState.toLocalPath(selectedFolder);
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: AppTheme.compact ? 1 : 3
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
                        columns: AppTheme.compact ? 1 : 3
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
                        Layout.fillWidth: true
                        columns: AppTheme.compact ? 1 : 4
                        flow: GridLayout.LeftToRight
                        uniformCellWidths: !AppTheme.compact
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
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: AppTheme.pageMargin
        implicitWidth: 56
        implicitHeight: 56
        text: Sentry.initialized ? "\u21bb" : "\u2192"
        tooltip: Sentry.initialized ? qsTr("Re-initialize") : qsTr("Initialize")
        font.pixelSize: Sentry.initialized ? 24 : 28
        font.weight: Font.DemiBold
        onClicked: page.initialize()
    }
}
