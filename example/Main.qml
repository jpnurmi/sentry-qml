import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Shapes
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
    color: theme.background

    readonly property bool compact: width < 900
    readonly property int pageMargin: compact ? 14 : 20
    readonly property int panelMargin: compact ? 14 : 18
    readonly property int controlHeight: 42
    readonly property int actionWidth: Math.max(152, Math.ceil(Math.max(initializeActionMetrics.width + 28, reinitializeActionMetrics.width + 28, sendActionMetrics.width + 28, addActionMetrics.width + 28, crashActionMetrics.width + 28)))
    property var attachmentHandles: []

    QtObject {
        id: theme

        readonly property color background: "#101014"
        readonly property color surface: "#1b1b20"
        readonly property color surfaceRaised: "#27272d"
        readonly property color input: "#2d2d33"
        readonly property color inputFocus: "#343440"
        readonly property color border: "#4a4a55"
        readonly property color text: "#f7f4ff"
        readonly property color muted: "#b8b2ca"
        readonly property color subtle: "#8d879d"
        readonly property color accent: "#7553ff"
        readonly property color accentHover: "#8468ff"
        readonly property color accentPressed: "#6442e8"
        readonly property color success: "#2ecc71"
        readonly property color warning: "#f2b84b"
        readonly property color info: "#70b8ff"
        readonly property color critical: "#e1567c"
        readonly property color criticalHover: "#ee6b91"
        readonly property color disabled: "#2b2c32"
        readonly property color disabledText: "#777183"
    }

    TextMetrics {
        id: initializeActionMetrics

        font.pixelSize: 14
        font.weight: Font.DemiBold
        text: qsTr("Initialize")
    }

    TextMetrics {
        id: reinitializeActionMetrics

        font.pixelSize: 14
        font.weight: Font.DemiBold
        text: qsTr("Re-initialize")
    }

    TextMetrics {
        id: sendActionMetrics

        font.pixelSize: 14
        font.weight: Font.DemiBold
        text: qsTr("Send")
    }

    TextMetrics {
        id: addActionMetrics

        font.pixelSize: 14
        font.weight: Font.DemiBold
        text: qsTr("Add")
    }

    TextMetrics {
        id: crashActionMetrics

        font.pixelSize: 14
        font.weight: Font.DemiBold
        text: qsTr("Crash")
    }

    component ConsentActionButton: Button {
        id: consentButton

        property bool giveConsent: true
        readonly property color actionColor: giveConsent ? theme.success : theme.critical

        text: giveConsent ? qsTr("Give") : qsTr("Revoke")
        hoverEnabled: true
        font.pixelSize: 14
        font.weight: Font.DemiBold
        implicitWidth: window.actionWidth
        implicitHeight: window.controlHeight
        contentItem: Text {
            text: consentButton.text
            color: consentButton.enabled ? theme.text : theme.disabledText
            font: consentButton.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 7
            border.width: 0
            color: {
                if (!consentButton.enabled)
                    return theme.disabled;
                if (consentButton.down)
                    return Qt.darker(consentButton.actionColor, 1.12);
                if (consentButton.hovered)
                    return Qt.lighter(consentButton.actionColor, 1.08);
                return consentButton.actionColor;
            }
        }
    }

    component ConsentStatusIcon: Item {
        id: consentIcon

        property int consent: Sentry.userConsent
        readonly property bool given: consent === Sentry.UserConsentGiven
        readonly property bool revoked: consent === Sentry.UserConsentRevoked
        readonly property color statusColor: given ? theme.success : revoked ? theme.critical : theme.warning

        implicitWidth: 28
        implicitHeight: 28

        Rectangle {
            visible: !consentIcon.given && !consentIcon.revoked
            width: 22
            height: 22
            radius: width / 2
            color: "transparent"
            border.width: 3
            border.color: consentIcon.statusColor
            anchors.centerIn: parent
        }

        Shape {
            id: unknownShape

            anchors.fill: parent
            visible: !consentIcon.given && !consentIcon.revoked
            antialiasing: true

            ShapePath {
                fillColor: "transparent"
                strokeColor: consentIcon.statusColor
                strokeWidth: 3
                capStyle: ShapePath.RoundCap
                startX: 20
                startY: 8

                PathLine {
                    x: 8
                    y: 20
                }
            }
        }

        Shape {
            anchors.fill: parent
            visible: consentIcon.given
            antialiasing: true

            ShapePath {
                fillColor: "transparent"
                strokeColor: consentIcon.statusColor
                strokeWidth: 4
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                startX: 5
                startY: 15

                PathLine {
                    x: 11
                    y: 21
                }
                PathLine {
                    x: 23
                    y: 7
                }
            }
        }

        Shape {
            anchors.fill: parent
            visible: consentIcon.revoked
            antialiasing: true

            ShapePath {
                fillColor: "transparent"
                strokeColor: consentIcon.statusColor
                strokeWidth: 4
                capStyle: ShapePath.RoundCap
                startX: 7
                startY: 7

                PathLine {
                    x: 21
                    y: 21
                }
            }

            ShapePath {
                fillColor: "transparent"
                strokeColor: consentIcon.statusColor
                strokeWidth: 4
                capStyle: ShapePath.RoundCap
                startX: 21
                startY: 7

                PathLine {
                    x: 7
                    y: 21
                }
            }
        }
    }

    component ConsentPanel: Rectangle {
        id: consentPanel

        readonly property bool consentActionable: Sentry.userConsentRequired

        implicitHeight: consentPanelLayout.implicitHeight + window.panelMargin
        radius: 8
        color: theme.surface

        ColumnLayout {
            id: consentPanelLayout

            anchors.fill: parent
            anchors.margins: window.panelMargin
            anchors.topMargin: 0
            spacing: 12

            Label {
                text: qsTr("CONSENT")
                color: theme.text
                font.pixelSize: 16
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
                Layout.fillWidth: true
                Layout.preferredHeight: 32
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                ConsentStatusIcon {
                    Layout.alignment: Qt.AlignVCenter
                    consent: Sentry.userConsent
                }

                Text {
                    text: consentFooterText()
                    color: consentPanel.consentActionable ? consentFooterColor() : theme.muted
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                ConsentActionButton {
                    giveConsent: Sentry.userConsent !== Sentry.UserConsentGiven
                    enabled: consentPanel.consentActionable
                    Layout.preferredWidth: window.actionWidth

                    onClicked: {
                        toggleUserConsent();
                    }
                }
            }
        }
    }

    component PageHeader: Item {
        id: header

        property bool canGoBack: false
        property bool showFeedbackButton: false
        signal backClicked
        signal feedbackClicked

        Layout.fillWidth: true
        implicitHeight: window.compact ? compactHeader.implicitHeight : desktopHeader.implicitHeight

        Item {
            id: desktopHeader

            readonly property real headerGap: 12
            readonly property real titleMaxWidth: parent.width
                - (headerActions.visible ? headerActions.implicitWidth + headerGap : 0)

            visible: !window.compact
            width: parent.width
            height: visible ? implicitHeight : 0
            implicitHeight: Math.max(titleRow.implicitHeight, headerActions.implicitHeight)

            RowLayout {
                id: titleRow

                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(0, Math.min(implicitWidth, desktopHeader.titleMaxWidth))
                spacing: 10
                clip: true

                BackButton {
                    visible: header.canGoBack

                    onClicked: {
                        header.backClicked();
                    }
                }

                Image {
                    Layout.preferredWidth: 42
                    Layout.preferredHeight: 37
                    Layout.leftMargin: 8
                    Layout.rightMargin: 4
                    source: "sentry-glyph.svg"
                    sourceSize.width: width
                    sourceSize.height: height
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                }

                Label {
                    text: qsTr("Sentry QML")
                    color: theme.text
                    font.pixelSize: 34
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
            }

            RowLayout {
                id: headerActions

                visible: header.showFeedbackButton
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10

                IconToolButton {
                    visible: header.showFeedbackButton
                    text: qsTr("Feedback")
                    Accessible.name: qsTr("Feedback")

                    onClicked: {
                        header.feedbackClicked();
                    }
                }
            }
        }

        ColumnLayout {
            id: compactHeader

            visible: window.compact
            width: parent.width
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                BackButton {
                    visible: header.canGoBack

                    onClicked: {
                        header.backClicked();
                    }
                }

                Image {
                    Layout.preferredWidth: 34
                    Layout.preferredHeight: 30
                    Layout.leftMargin: 4
                    Layout.rightMargin: 2
                    source: "sentry-glyph.svg"
                    sourceSize.width: width
                    sourceSize.height: height
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                }

                Label {
                    text: qsTr("Sentry QML")
                    color: theme.text
                    font.pixelSize: 28
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                IconToolButton {
                    visible: header.showFeedbackButton
                    text: qsTr("Feedback")
                    Accessible.name: qsTr("Feedback")

                    onClicked: {
                        header.feedbackClicked();
                    }
                }
            }

        }
    }

    component TabButton: Button {
        id: tabButton

        property bool selected: false

        hoverEnabled: true
        font.pixelSize: 16
        font.weight: Font.DemiBold
        padding: 0
        leftPadding: window.panelMargin
        rightPadding: window.panelMargin
        topPadding: 0
        bottomPadding: 0
        implicitWidth: tabText.implicitWidth + leftPadding + rightPadding
        implicitHeight: 32
        contentItem: Text {
            id: tabText

            text: tabButton.text.toUpperCase()
            color: tabButton.selected ? theme.text : theme.subtle
            font: tabButton.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Item {
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 2
                radius: 1
                color: theme.accent
                opacity: tabButton.selected ? 1 : tabButton.hovered ? 0.28 : 0
            }
        }
    }

    component LevelComboBox: ComboBox {
        id: combo

        property string levelValue: currentText.toLowerCase()

        function levelColor(index) {
            if (index === 1)
                return theme.warning;
            if (index === 2)
                return theme.critical;
            return theme.info;
        }

        model: [qsTr("Info"), qsTr("Warning"), qsTr("Error")]
        font.pixelSize: 14
        implicitHeight: 32
        //implicitWidth: levelMetrics.width + leftPadding + rightPadding + 34
        leftPadding: 0
        rightPadding: 28
        contentItem: RowLayout {
            spacing: 8

            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: 8
                implicitHeight: 8
                radius: 4
                color: combo.levelColor(combo.currentIndex)
            }

            Text {
                text: combo.displayText
                color: theme.text
                font: combo.font
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
        indicator: ComboChevron {
            x: combo.width - width - 12
            y: (combo.height - height) / 2
            strokeColor: combo.hovered || combo.popup.visible ? theme.text : theme.muted
        }
        delegate: ItemDelegate {
            id: comboDelegate

            required property string modelData
            required property int index

            width: levelMetrics.width + leftPadding + rightPadding + 34
            height: 38
            hoverEnabled: true
            highlighted: combo.highlightedIndex === index
            contentItem: RowLayout {
                spacing: 8

                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: 8
                    implicitHeight: 8
                    radius: 4
                    color: combo.levelColor(comboDelegate.index)
                }

                Text {
                    text: comboDelegate.modelData
                    color: comboDelegate.highlighted || combo.currentIndex === comboDelegate.index ? theme.text : theme.muted
                    font: combo.font
                    verticalAlignment: Text.AlignVCenter
                    Layout.fillWidth: true
                }
            }
            background: Rectangle {
                color: comboDelegate.highlighted ? "#343440" : combo.currentIndex === comboDelegate.index ? "#2d2d33" : "transparent"
            }
        }
        background: Rectangle {
            color: "transparent"
        }
        popup: Popup {
            y: combo.height + 4
            width: levelMetrics.width + leftPadding + rightPadding + 34
            implicitHeight: contentItem.implicitHeight + 8
            padding: 4

            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: combo.popup.visible ? combo.delegateModel : null
                currentIndex: combo.highlightedIndex
            }
            background: Rectangle {
                color: theme.input
                border.color: theme.border
                radius: 7
            }
        }

        TextMetrics {
            id: levelMetrics

            font: combo.font
            text: qsTr("Warning")
        }
    }

    component CaptureOptionComboBox: ComboBox {
        id: optionCombo

        property string widthText: displayText
        property var disabledIndexes: []

        function indexEnabled(index) {
            return disabledIndexes.indexOf(index) === -1;
        }

        font.pixelSize: 14
        implicitWidth: optionComboMetrics.width + leftPadding + rightPadding + 24
        implicitHeight: window.controlHeight
        leftPadding: 12
        rightPadding: 30
        Layout.fillWidth: false
        contentItem: Text {
            text: optionCombo.displayText
            color: theme.text
            font: optionCombo.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        indicator: ComboChevron {
            x: optionCombo.width - width - 12
            y: (optionCombo.height - height) / 2
            strokeColor: optionCombo.hovered || optionCombo.popup.visible ? theme.text : theme.muted
        }
        delegate: ItemDelegate {
            id: optionDelegate

            required property string modelData
            required property int index

            width: optionCombo.width
            height: 38
            enabled: optionCombo.indexEnabled(index)
            hoverEnabled: true
            highlighted: optionCombo.highlightedIndex === index
            contentItem: Text {
                text: optionDelegate.modelData
                color: !optionDelegate.enabled ? theme.disabledText : optionDelegate.highlighted || optionCombo.currentIndex === optionDelegate.index ? theme.text : theme.muted
                font: optionCombo.font
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            background: Rectangle {
                color: !optionDelegate.enabled ? "transparent" : optionDelegate.highlighted ? "#343440" : optionCombo.currentIndex === optionDelegate.index ? "#2d2d33" : "transparent"
            }
        }
        background: Rectangle {
            color: optionCombo.hovered ? theme.inputFocus : theme.input
            border.color: theme.border
            radius: 7
        }
        popup: Popup {
            y: optionCombo.height + 4
            width: optionCombo.width
            implicitHeight: Math.min(contentItem.implicitHeight + 8, 320)
            padding: 4

            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: optionCombo.popup.visible ? optionCombo.delegateModel : null
                currentIndex: optionCombo.highlightedIndex
            }
            background: Rectangle {
                color: theme.input
                border.color: theme.border
                radius: 7
            }
        }

        TextMetrics {
            id: optionComboMetrics

            font: optionCombo.font
            text: optionCombo.widthText
        }
    }

    component ScopeEntriesView: ColumnLayout {
        id: entriesView

        property var model
        property int scopeTab: 0
        readonly property int headerHeight: 34
        readonly property int rowHeight: 34
        readonly property int tablePadding: 14
        readonly property int removeColumnWidth: 42
        readonly property int keyColumnWidth: Math.round((tableFrame.width - tablePadding * 2 - removeColumnWidth) * 0.42)
        readonly property int valueColumnX: tablePadding + keyColumnWidth

        function removeEntry(index, key) {
            if (!ensureInitialized())
                return;

            const ok = scopeTab === 0 ? Sentry.removeTag(key) : Sentry.removeContext(key);
            if (ok)
                model.remove(index);
            setStatus(ok ? scopeTab === 0 ? qsTr("Tag removed") : qsTr("Context removed") : scopeTab === 0 ? qsTr("Tag was not removed") : qsTr("Context was not removed"), ok);
        }

        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: 0

        Rectangle {
            id: tableFrame

            Layout.fillWidth: true
            Layout.fillHeight: true
            implicitHeight: window.compact ? 148 : 124
            color: "#151518"
            border.color: "#24242b"
            radius: 7
            clip: true

            Column {
                anchors.fill: parent
                spacing: 0

                Item {
                    width: parent.width
                    height: entriesView.headerHeight

                    Label {
                        text: qsTr("Key")
                        color: theme.text
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        x: entriesView.tablePadding
                        width: entriesView.keyColumnWidth - 8
                        height: parent.height
                        verticalAlignment: Text.AlignVCenter
                        horizontalAlignment: Text.AlignLeft
                        elide: Text.ElideRight
                    }

                    Label {
                        text: qsTr("Value")
                        color: theme.text
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        x: entriesView.valueColumnX
                        width: parent.width - x - entriesView.removeColumnWidth - entriesView.tablePadding
                        height: parent.height
                        verticalAlignment: Text.AlignVCenter
                        horizontalAlignment: Text.AlignLeft
                        elide: Text.ElideRight
                    }
                }

                Rectangle {
                    width: parent.width
                    height: 1
                    color: "#24242b"
                }

                Repeater {
                    model: entriesView.model

                    delegate: Item {
                        id: entryRow

                        required property int index
                        required property string entryKey
                        required property string entryValue

                        width: parent.width
                        height: entriesView.rowHeight

                        Label {
                            text: entryKey
                            color: theme.text
                            font.pixelSize: 15
                            x: entriesView.tablePadding
                            width: entriesView.keyColumnWidth - 8
                            height: parent.height
                            verticalAlignment: Text.AlignVCenter
                            horizontalAlignment: Text.AlignLeft
                            elide: Text.ElideRight
                        }

                        Label {
                            text: entryValue
                            color: theme.text
                            font.pixelSize: 15
                            x: entriesView.valueColumnX
                            width: parent.width - x - entriesView.removeColumnWidth - entriesView.tablePadding
                            height: parent.height
                            verticalAlignment: Text.AlignVCenter
                            horizontalAlignment: Text.AlignLeft
                            elide: Text.ElideRight
                        }

                        IconToolButton {
                            text: "×"
                            tooltip: entriesView.scopeTab === 0 ? qsTr("Remove tag") : qsTr("Remove context")
                            destructive: true
                            quietDestructive: true
                            enabled: Sentry.initialized
                            anchors.right: parent.right
                            anchors.rightMargin: entriesView.tablePadding - 4
                            anchors.verticalCenter: parent.verticalCenter

                            onClicked: {
                                entriesView.removeEntry(entryRow.index, entryRow.entryKey);
                            }
                        }
                    }
                }

                Item {
                    width: parent.width
                    height: Math.max(0, tableFrame.height - entriesView.headerHeight - 1 - entriesView.model.count * entriesView.rowHeight)
                }
            }
        }
    }

    component AttachmentEntriesView: ColumnLayout {
        id: attachmentsView

        property var model
        readonly property int headerHeight: 34
        readonly property int rowHeight: 34
        readonly property int tablePadding: 14
        readonly property int removeColumnWidth: 42
        readonly property int sizeColumnWidth: Math.min(120, Math.max(86, Math.round(tableFrame.width * 0.18)))
        readonly property int sizeColumnX: tableFrame.width - tablePadding - removeColumnWidth - sizeColumnWidth
        readonly property int fileColumnWidth: Math.max(80, sizeColumnX - tablePadding - 12)

        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: 0

        Rectangle {
            id: tableFrame

            Layout.fillWidth: true
            Layout.fillHeight: true
            implicitHeight: window.compact ? 148 : 124
            color: "#151518"
            border.color: "#24242b"
            radius: 7
            clip: true

            Column {
                anchors.fill: parent
                spacing: 0

                Item {
                    width: parent.width
                    height: attachmentsView.headerHeight

                    Label {
                        text: qsTr("File")
                        color: theme.text
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        x: attachmentsView.tablePadding
                        width: attachmentsView.fileColumnWidth - 8
                        height: parent.height
                        verticalAlignment: Text.AlignVCenter
                        horizontalAlignment: Text.AlignLeft
                        elide: Text.ElideRight
                    }

                    Label {
                        text: qsTr("Size")
                        color: theme.text
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        x: attachmentsView.sizeColumnX
                        width: attachmentsView.sizeColumnWidth
                        height: parent.height
                        verticalAlignment: Text.AlignVCenter
                        horizontalAlignment: Text.AlignRight
                        elide: Text.ElideRight
                    }
                }

                Rectangle {
                    width: parent.width
                    height: 1
                    color: "#24242b"
                }

                Repeater {
                    model: attachmentsView.model

                    delegate: Item {
                        id: attachmentRow

                        required property int index
                        required property string entryKey
                        required property string entrySize

                        width: parent.width
                        height: attachmentsView.rowHeight

                        Label {
                            text: entryKey
                            color: theme.text
                            font.pixelSize: 15
                            x: attachmentsView.tablePadding
                            width: attachmentsView.fileColumnWidth - 8
                            height: parent.height
                            verticalAlignment: Text.AlignVCenter
                            horizontalAlignment: Text.AlignLeft
                            elide: Text.ElideRight
                        }

                        Label {
                            text: entrySize
                            color: theme.text
                            font.pixelSize: 15
                            x: attachmentsView.sizeColumnX
                            width: attachmentsView.sizeColumnWidth
                            height: parent.height
                            verticalAlignment: Text.AlignVCenter
                            horizontalAlignment: Text.AlignRight
                            elide: Text.ElideRight
                        }

                        IconToolButton {
                            text: "×"
                            tooltip: qsTr("Remove attachment")
                            destructive: true
                            quietDestructive: true
                            enabled: Sentry.initialized
                            anchors.right: parent.right
                            anchors.rightMargin: attachmentsView.tablePadding - 4
                            anchors.verticalCenter: parent.verticalCenter

                            onClicked: {
                                removeAttachmentAt(attachmentRow.index);
                            }
                        }
                    }
                }

                Item {
                    width: parent.width
                    height: Math.max(0, tableFrame.height - attachmentsView.headerHeight - 1 - attachmentsView.model.count * attachmentsView.rowHeight)
                }
            }
        }
    }

    SentryOptions {
        id: sentryOptions

        dsn: ExampleState.dsn
        databasePath: ExampleState.databasePath
        release: ExampleState.release
        environment: ExampleState.environment
        dist: ExampleState.dist
        debug: ExampleState.debugEnabled
        enableLogs: ExampleState.logsEnabled
        enableMetrics: ExampleState.metricsEnabled
        autoSessionTracking: ExampleState.autoSessionTrackingEnabled
        requireUserConsent: ExampleState.requireUserConsentEnabled
        attachViewHierarchy: ExampleState.viewHierarchyEnabled
        sampleRate: ExampleState.sampleRate
        maxBreadcrumbs: ExampleState.maxBreadcrumbs
        shutdownTimeout: ExampleState.shutdownTimeout
        user: SentryUser {
            userId: ExampleState.userId
            username: ExampleState.username
            email: ExampleState.email
            ipAddress: ExampleState.ipAddress
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

    ListModel {
        id: tagEntries

        ListElement {
            entryKey: "backend"
            entryValue: "qml"
        }
    }

    ListModel {
        id: contextEntries

        ListElement {
            entryKey: "example"
            entryValue: "qml"
        }
    }

    ListModel {
        id: attachmentEntries
    }

    function statusSeverity(message, ok) {
        return ok ? 1 : 2;
    }

    function setStatus(message, ok, severity) {
        ExampleState.setStatus(message, ok, severity === undefined ? statusSeverity(message, ok) : severity);
    }

    function isInitializeStatus(message) {
        return message === qsTr("Initialization failed") || message === qsTr("Re-initialization failed");
    }

    function resetRuntimeStatus() {
        if (!isInitializeStatus(ExampleState.statusMessage))
            setStatus(qsTr("Ready"), Sentry.initialized, Sentry.initialized ? 1 : 0);
    }

    function globalStatusText() {
        if (isInitializeStatus(ExampleState.statusMessage))
            return ExampleState.statusMessage;
        if (ExampleState.statusMessage !== qsTr("Ready"))
            return ExampleState.statusMessage;
        return Sentry.initialized ? qsTr("Ready") : qsTr("Not initialized");
    }

    function globalStatusSeverity() {
        if (isInitializeStatus(ExampleState.statusMessage))
            return 2;
        if (ExampleState.statusMessage !== qsTr("Ready"))
            return ExampleState.statusSeverity;
        return Sentry.initialized ? 1 : 0;
    }

    function upsertEntry(model, key, value) {
        for (let i = 0; i < model.count; ++i) {
            if (model.get(i).entryKey === key) {
                model.set(i, {
                    entryKey: key,
                    entryValue: value
                });
                return;
            }
        }
        model.append({
            entryKey: key,
            entryValue: value
        });
    }

    function fileNameFromPath(path) {
        const localPath = ExampleState.toLocalPath(path);
        const normalized = localPath.replace(/\\/g, "/");
        const index = normalized.lastIndexOf("/");
        return index >= 0 ? normalized.substring(index + 1) : normalized;
    }

    function formattedAttachmentSize(size) {
        const bytes = Number(size);
        return bytes >= 0 ? Qt.locale().formattedDataSize(bytes) : qsTr("Unknown");
    }

    function clearAttachmentEntries() {
        attachmentHandles = [];
        attachmentEntries.clear();
    }

    function addAttachment(path) {
        if (!ensureInitialized())
            return;

        const localPath = ExampleState.toLocalPath(path);
        const attachment = Sentry.attachFile(localPath);
        const ok = attachment && attachment.valid;
        if (ok) {
            const handles = attachmentHandles.slice();
            handles.push(attachment);
            attachmentHandles = handles;
            const filename = String(attachment.filename || "");
            attachmentEntries.append({
                entryKey: filename.length > 0 ? filename : fileNameFromPath(localPath),
                entrySize: formattedAttachmentSize(attachment.size)
            });
        }
        setStatus(ok ? qsTr("Attachment added") : qsTr("Attachment was not added"), ok);
    }

    function removeAttachmentAt(index) {
        if (!ensureInitialized())
            return;

        const attachment = attachmentHandles[index];
        const ok = attachment && Sentry.removeAttachment(attachment);
        if (ok) {
            const handles = attachmentHandles.slice();
            handles.splice(index, 1);
            attachmentHandles = handles;
            attachmentEntries.remove(index);
        }
        setStatus(ok ? qsTr("Attachment removed") : qsTr("Attachment was not removed"), ok);
    }

    function initializeSentry() {
        if (Sentry.initialized && !Sentry.close()) {
            setStatus(qsTr("Re-initialization failed"), false);
            return false;
        }

        const ok = Sentry.init(sentryOptions);
        if (ok) {
            ExampleState.sessionActive = false;
            clearAttachmentEntries();
            setStatus(qsTr("Ready"), true);
            if (pageStack.depth === 1)
                pageStack.push(runtimePageComponent);
        } else {
            setStatus(qsTr("Initialization failed"), false);
        }
        return ok;
    }

    function ensureInitialized() {
        return Sentry.initialized || initializeSentry();
    }

    function capture() {
        if (!ensureInitialized())
            return;
        if (ExampleState.captureMode === 1) {
            captureException();
        } else if (ExampleState.captureMode === 2) {
            addBreadcrumb();
        } else {
            captureMessage();
        }
    }

    function captureMessage() {
        const eventId = Sentry.captureMessage(ExampleState.messageText, ExampleState.captureLevel());
        setStatus(eventId.length > 0 ? qsTr("Captured event %1").arg(eventId) : qsTr("Message was not captured"), eventId.length > 0);
    }

    function applyScope() {
        if (!ensureInitialized())
            return;
        if (ExampleState.scopeTab === 0) {
            const tagKey = ExampleState.tagKey.trim();
            if (tagKey.length === 0) {
                setStatus(qsTr("Tag key is required"), false);
                return;
            }

            const ok = Sentry.setTag(tagKey, ExampleState.tagValue);
            if (ok)
                upsertEntry(tagEntries, tagKey, ExampleState.tagValue);
            setStatus(ok ? qsTr("Tag added") : qsTr("Tag was not added"), ok);
        } else if (ExampleState.scopeTab === 1) {
            const contextKey = ExampleState.contextKey.trim();
            if (contextKey.length === 0) {
                setStatus(qsTr("Context key is required"), false);
                return;
            }

            const ok = Sentry.setContext(contextKey, {
                value: ExampleState.contextValue,
                messageLength: ExampleState.messageText.length
            });
            if (ok)
                upsertEntry(contextEntries, contextKey, ExampleState.contextValue);
            setStatus(ok ? qsTr("Context added") : qsTr("Context was not added"), ok);
        }
    }

    function syncUser() {
        if (!Sentry.initialized)
            return;
        const ok = Sentry.setUser({
            id: ExampleState.userId,
            username: ExampleState.username,
            email: ExampleState.email,
            ipAddress: ExampleState.ipAddress
        });
        if (!ok)
            setStatus(qsTr("User was not updated"), false);
    }

    function consentFooterColor() {
        if (!Sentry.userConsentRequired)
            return theme.surfaceRaised;
        if (Sentry.userConsent === Sentry.UserConsentGiven)
            return theme.success;
        if (Sentry.userConsent === Sentry.UserConsentRevoked)
            return theme.critical;
        return "#9b6b17";
    }

    function consentFooterText() {
        if (!Sentry.userConsentRequired)
            return qsTr("Not required");
        if (Sentry.userConsent === Sentry.UserConsentGiven)
            return qsTr("Given — events will be sent to Sentry");
        if (Sentry.userConsent === Sentry.UserConsentRevoked)
            return qsTr("Revoked — events will not be sent to Sentry");
        return qsTr("Unknown — events will not be sent to Sentry");
    }

    function toggleUserConsent() {
        if (!Sentry.userConsentRequired) {
            setStatus(qsTr("Consent is not required"), false);
            return;
        }

        const giveConsent = Sentry.userConsent !== Sentry.UserConsentGiven;
        const ok = giveConsent ? Sentry.giveUserConsent() : Sentry.revokeUserConsent();
        if (ok)
            setStatus(giveConsent ? qsTr("User consent given") : qsTr("User consent revoked"), true, giveConsent ? 1 : 2);
        else
            setStatus(giveConsent ? qsTr("User consent was not given") : qsTr("User consent was not revoked"), false);
    }

    function sendFeedback(name, email, message) {
        if (!ensureInitialized())
            return false;

        const feedbackMessage = message.trim();
        if (feedbackMessage.length === 0) {
            setStatus(qsTr("Feedback message is required"), false);
            return false;
        }

        const feedback = {
            message: feedbackMessage
        };
        const feedbackName = name.trim();
        const feedbackEmail = email.trim();
        if (feedbackName.length > 0)
            feedback.name = feedbackName;
        if (feedbackEmail.length > 0)
            feedback.email = feedbackEmail;

        const ok = Sentry.captureFeedback(feedback);
        setStatus(ok ? qsTr("Feedback sent") : qsTr("Feedback was not sent"), ok);
        return ok;
    }

    function startSession() {
        if (!ensureInitialized())
            return;
        let ok = true;
        const release = ExampleState.sessionRelease.trim();
        const environment = ExampleState.sessionEnvironment.trim();

        if (release.length > 0)
            ok = Sentry.setRelease(release) && ok;
        if (environment.length > 0)
            ok = Sentry.setEnvironment(environment) && ok;

        ok = Sentry.startSession() && ok;
        if (ok)
            ExampleState.sessionActive = true;
        setStatus(ok ? qsTr("Session started") : qsTr("Session was not started"), ok);
    }

    function endSession() {
        if (!ensureInitialized())
            return;
        const ok = Sentry.endSession(Sentry.SessionExited);
        if (ok)
            ExampleState.sessionActive = false;
        setStatus(ok ? qsTr("Session ended") : qsTr("Session was not ended"), ok);
    }

    function toggleSession() {
        if (ExampleState.sessionActive)
            endSession();
        else
            startSession();
    }

    function addBreadcrumb() {
        const ok = Sentry.addBreadcrumb({
            message: ExampleState.messageText,
            category: ExampleState.breadcrumbCategory(),
            type: "manual",
            level: ExampleState.captureLevel(),
            data: {
                message: ExampleState.messageText
            }
        });
        setStatus(ok ? qsTr("Breadcrumb added") : qsTr("Breadcrumb was not added"), ok);
    }

    function captureException() {
        if (ExampleState.exceptionKind() === "native") {
            setStatus(qsTr("Native exception capture is not available"), false);
            return;
        }

        try {
            throw new Error(ExampleState.messageText);
        } catch (exception) {
            const eventId = Sentry.captureException(exception);
            setStatus(eventId.length > 0 ? qsTr("Captured exception %1").arg(eventId) : qsTr("Exception was not captured"), eventId.length > 0);
        }
    }

    function triggerQmlError() {
        callMissingQmlFunction();
    }

    function triggerCrash() {
        if (ExampleState.crashKindIndex === 0) {
            setStatus(qsTr("Crashing..."), false);
            exampleActions.crash();
        } else {
            setStatus(qsTr("Triggering QML error..."), false);
            triggerQmlError();
        }
    }

    function callMissingQmlFunction() {
        missingQmlFunction();
    }

    Connections {
        target: Sentry

        function onErrorOccurred(message) {
            setStatus(message, false);
        }
    }

    Popup {
        id: scopeEditorPopup

        property int scopeTab: 0

        function openFor(tab) {
            scopeTab = tab;
            scopeKeyField.text = "";
            scopeValueField.text = "";
            open();
        }

        x: (window.width - width) / 2
        y: Math.max(window.pageMargin, (window.height - height) / 2)
        width: Math.min(window.width - window.pageMargin * 2, 420)
        modal: true
        focus: true
        padding: window.panelMargin
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        contentItem: ColumnLayout {
            spacing: 14

            Label {
                text: scopeEditorPopup.scopeTab === 0 ? qsTr("Add tag") : qsTr("Add context")
                color: theme.text
                font.pixelSize: 16
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }

            LabeledTextField {
                id: scopeKeyField

                label: scopeEditorPopup.scopeTab === 0 ? qsTr("Key") : qsTr("Context")
                placeholderText: scopeEditorPopup.scopeTab === 0 ? qsTr("feature") : qsTr("device")
            }

            LabeledTextField {
                id: scopeValueField

                label: qsTr("Value")
                placeholderText: scopeEditorPopup.scopeTab === 0 ? qsTr("checkout") : qsTr("mobile")
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Item {
                    Layout.fillWidth: true
                }

                ActionButton {
                    text: qsTr("Cancel")

                    onClicked: {
                        scopeEditorPopup.close();
                    }
                }

                ActionButton {
                    text: qsTr("Add")
                    primary: true

                    onClicked: {
                        if (scopeEditorPopup.scopeTab === 0) {
                            ExampleState.tagKey = scopeKeyField.text;
                            ExampleState.tagValue = scopeValueField.text;
                        } else {
                            ExampleState.contextKey = scopeKeyField.text;
                            ExampleState.contextValue = scopeValueField.text;
                        }
                        ExampleState.scopeTab = scopeEditorPopup.scopeTab;
                        applyScope();
                        scopeEditorPopup.close();
                    }
                }
            }
        }

        background: Rectangle {
            color: theme.surface
            border.color: theme.border
            radius: 8
        }
    }

    FileDialog {
        id: attachmentFileDialog

        title: qsTr("Attach file")
        currentFolder: ExampleState.toFileUrl(ExampleState.databasePath)

        onAccepted: {
            addAttachment(selectedFile);
        }
    }

    Popup {
        id: feedbackPopup

        function openFeedback() {
            feedbackNameField.text = "";
            feedbackEmailField.text = ExampleState.email;
            feedbackMessageArea.text = "";
            open();
        }

        x: (window.width - width) / 2
        y: Math.max(window.pageMargin, (window.height - height) / 2)
        width: Math.min(window.width - window.pageMargin * 2, 520)
        modal: true
        focus: true
        padding: window.panelMargin
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        contentItem: ColumnLayout {
            spacing: 14

            Label {
                text: qsTr("Feedback")
                color: theme.text
                font.pixelSize: 16
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }

            GridLayout {
                Layout.fillWidth: true
                columns: window.compact ? 1 : 2
                uniformCellWidths: true
                rowSpacing: 10
                columnSpacing: 10

                LabeledTextField {
                    id: feedbackNameField

                    label: qsTr("Name")
                    placeholderText: qsTr("Jane")
                }

                LabeledTextField {
                    id: feedbackEmailField

                    label: qsTr("Email")
                    placeholderText: qsTr("jane@example.com")
                    inputMethodHints: Qt.ImhEmailCharactersOnly
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6

                Label {
                    text: qsTr("Message")
                    color: theme.muted
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                }

                TextArea {
                    id: feedbackMessageArea

                    placeholderText: qsTr("What happened?")
                    placeholderTextColor: theme.subtle
                    color: theme.text
                    selectedTextColor: theme.text
                    selectionColor: theme.accent
                    font.pixelSize: 15
                    wrapMode: TextArea.Wrap
                    leftPadding: 12
                    rightPadding: 12
                    topPadding: 10
                    bottomPadding: 10
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(220, Math.max(130, window.height * 0.24))

                    background: Rectangle {
                        color: feedbackMessageArea.activeFocus ? theme.inputFocus : theme.input
                        border.color: feedbackMessageArea.activeFocus ? theme.accent : theme.border
                        border.width: feedbackMessageArea.activeFocus ? 2 : 1
                        radius: 7
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Item {
                    Layout.fillWidth: true
                }

                ActionButton {
                    text: qsTr("Cancel")

                    onClicked: {
                        feedbackPopup.close();
                    }
                }

                ActionButton {
                    text: qsTr("Send")
                    primary: true
                    enabled: feedbackMessageArea.text.trim().length > 0

                    onClicked: {
                        if (sendFeedback(feedbackNameField.text, feedbackEmailField.text, feedbackMessageArea.text))
                            feedbackPopup.close();
                    }
                }
            }
        }

        background: Rectangle {
            color: theme.surface
            border.color: theme.border
            radius: 8
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        StackView {
            id: pageStack

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            initialItem: initializePageComponent
        }
    }

    Popup {
        id: globalStatusPopup

        parent: Overlay.overlay
        modal: false
        dim: false
        focus: false
        closePolicy: Popup.NoAutoClose
        padding: 0
        visible: true
        width: globalStatusPill.implicitWidth
        height: globalStatusPill.implicitHeight
        x: parent ? Math.max(window.pageMargin, parent.width - width - window.pageMargin) : window.pageMargin
        y: window.pageMargin
        background: Item {}
        contentItem: Banner {
            id: globalStatusPill

            severity: globalStatusSeverity()
            text: globalStatusText()
        }
    }

    Component {
        id: initializePageComponent

        Item {
            width: pageStack.width
            height: pageStack.height

            ScrollView {
                id: initializeScrollView

                anchors.fill: parent
                clip: true
                padding: window.pageMargin
                contentWidth: availableWidth
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                ColumnLayout {
                    id: initializePage

                    width: initializeScrollView.availableWidth
                    spacing: 16

                        PageHeader {
                            id: initializeHeader

                            canGoBack: false
                        }

                        Rectangle {
                            id: initializePanel

                            Layout.fillWidth: true
                            Layout.bottomMargin: window.pageMargin
                            implicitHeight: setupLayout.implicitHeight + window.panelMargin
                            color: theme.surface
                            radius: 8

                            ColumnLayout {
                                id: setupLayout

                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: window.panelMargin
                                anchors.topMargin: 0
                                spacing: 14

                                Label {
                                    id: initializeTitle

                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 32
                                    text: qsTr("OPTIONS")
                                    color: theme.text
                                    font.pixelSize: 16
                                    font.weight: Font.DemiBold
                                    verticalAlignment: Text.AlignVCenter
                                }

                                LabeledTextField {
                                    id: dsnOptionField

                                    label: qsTr("DSN")
                                    text: ExampleState.dsn
                                    placeholderText: qsTr("https://public@example.ingest.sentry.io/1")
                                    onTextEdited: ExampleState.dsn = text
                                }

                                GridLayout {
                                    id: databaseRow

                                    Layout.fillWidth: true
                                    columns: 2
                                    rowSpacing: 10
                                    columnSpacing: 10

                                    LabeledTextField {
                                        label: qsTr("Database")
                                        text: ExampleState.databasePath
                                        placeholderText: qsTr("/path/to/sentry-db")
                                        onTextEdited: ExampleState.databasePath = text
                                    }

                                    ActionButton {
                                        text: qsTr("Browse...")
                                        Layout.alignment: Qt.AlignBottom | Qt.AlignRight

                                        onClicked: {
                                            databaseFolderDialog.currentFolder = ExampleState.toFileUrl(ExampleState.databasePath);
                                            databaseFolderDialog.open();
                                        }
                                    }
                                }

                                FolderDialog {
                                    id: databaseFolderDialog

                                    title: qsTr("Choose database")
                                    currentFolder: ExampleState.toFileUrl(ExampleState.databasePath)

                                    onAccepted: {
                                        ExampleState.databasePath = ExampleState.toLocalPath(selectedFolder);
                                    }
                                }

                                GridLayout {
                                    id: releaseRow

                                    Layout.fillWidth: true
                                    columns: window.compact ? 1 : 3
                                    uniformCellWidths: true
                                    rowSpacing: 10
                                    columnSpacing: 10

                                    LabeledTextField {
                                        label: qsTr("Release")
                                        text: ExampleState.release
                                        placeholderText: qsTr("my-app@1.0.0")
                                        onTextEdited: ExampleState.release = text
                                        Layout.fillWidth: true
                                    }

                                    LabeledTextField {
                                        label: qsTr("Environment")
                                        text: ExampleState.environment
                                        placeholderText: qsTr("production")
                                        onTextEdited: ExampleState.environment = text
                                        Layout.fillWidth: true
                                    }

                                    LabeledTextField {
                                        label: qsTr("Distribution")
                                        text: ExampleState.dist
                                        placeholderText: qsTr("1")
                                        onTextEdited: ExampleState.dist = text
                                        Layout.fillWidth: true
                                    }
                                }

                                GridLayout {
                                    id: limitsRow

                                    Layout.fillWidth: true
                                    columns: window.compact ? 1 : 3
                                    uniformCellWidths: true
                                    rowSpacing: 10
                                    columnSpacing: 10

                                    LabeledDoubleSpinBox {
                                        label: qsTr("Sample rate")
                                        from: 0.0
                                        to: 1.0
                                        value: ExampleState.sampleRate
                                        stepSize: 0.1
                                        decimals: 1
                                        onValueModified: ExampleState.sampleRate = value
                                        locale: Qt.locale("en_US")
                                    }

                                    LabeledSpinBox {
                                        label: qsTr("Max breadcrumbs")
                                        from: 0
                                        to: 1000
                                        value: ExampleState.maxBreadcrumbs
                                        stepSize: 1
                                        onValueModified: ExampleState.maxBreadcrumbs = value
                                    }

                                    LabeledSpinBox {
                                        label: qsTr("Shutdown timeout (ms)")
                                        from: 0
                                        to: 60000
                                        value: ExampleState.shutdownTimeout
                                        stepSize: 100
                                        onValueModified: ExampleState.shutdownTimeout = value
                                    }
                                }

                                GridLayout {
                                    id: featureFlow

                                    Layout.fillWidth: true
                                    columns: window.compact ? 1 : 4
                                    flow: GridLayout.LeftToRight
                                    uniformCellWidths: true
                                    rowSpacing: 10
                                    columnSpacing: 16

                                    CheckBox {
                                        text: qsTr("Debug")
                                        checked: ExampleState.debugEnabled
                                        onToggled: ExampleState.debugEnabled = checked
                                        Layout.fillWidth: true
                                    }

                                    CheckBox {
                                        text: qsTr("Logs")
                                        checked: ExampleState.logsEnabled
                                        onToggled: ExampleState.logsEnabled = checked
                                        Layout.fillWidth: true
                                    }

                                    CheckBox {
                                        text: qsTr("Metrics")
                                        checked: ExampleState.metricsEnabled
                                        onToggled: ExampleState.metricsEnabled = checked
                                        Layout.fillWidth: true
                                    }

                                    CheckBox {
                                        text: qsTr("Auto sessions")
                                        checked: ExampleState.autoSessionTrackingEnabled
                                        onToggled: ExampleState.autoSessionTrackingEnabled = checked
                                        Layout.fillWidth: true
                                    }

                                    CheckBox {
                                        text: qsTr("Require consent")
                                        checked: ExampleState.requireUserConsentEnabled
                                        onToggled: ExampleState.requireUserConsentEnabled = checked
                                        Layout.fillWidth: true
                                    }

                                    CheckBox {
                                        text: qsTr("View hierarchy")
                                        checked: ExampleState.viewHierarchyEnabled
                                        onToggled: ExampleState.viewHierarchyEnabled = checked
                                        Layout.fillWidth: true
                                    }
                                }

                            }

                        }
                    }
                }

                FloatingActionButton {
                    id: initializeButton

                    readonly property string actionText: Sentry.initialized ? qsTr("Re-initialize") : qsTr("Initialize")

                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: window.pageMargin
                    implicitWidth: 56
                    implicitHeight: 56
                    text: Sentry.initialized ? "\u21bb" : "\u2192"
                    tooltip: actionText
                    font.pixelSize: Sentry.initialized ? 24 : 28
                    font.weight: Font.DemiBold

                    onClicked: {
                        initializeSentry();
                    }
                }
            }
        }

        Component {
            id: runtimePageComponent

            Item {
                width: pageStack.width
                height: pageStack.height

                ScrollView {
                    id: runtimeScrollView

                    anchors.fill: parent
                    clip: true
                    padding: window.pageMargin
                    contentWidth: availableWidth
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    ScrollBar.vertical.policy: ScrollBar.AsNeeded

                    ColumnLayout {
                        id: runtimePage

                        width: runtimeScrollView.availableWidth
                        spacing: 16

                        PageHeader {
                            canGoBack: true

                            onBackClicked: {
                                resetRuntimeStatus();
                                pageStack.pop();
                            }
                        }

                        ConsentPanel {
                            visible: ExampleState.requireUserConsentEnabled && Sentry.initialized
                            Layout.fillWidth: true
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: captureLayout.implicitHeight + window.panelMargin
                            color: theme.surface
                            radius: 8

                            ColumnLayout {
                                id: captureLayout

                                readonly property real sideControlWidth: window.actionWidth
                                readonly property real optionControlWidth: Math.max(exceptionKindCombo.implicitWidth, breadcrumbCategoryCombo.implicitWidth)

                                anchors.fill: parent
                                anchors.margins: window.panelMargin
                                anchors.topMargin: 0
                                spacing: 12

                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.leftMargin: -window.panelMargin
                                    Layout.rightMargin: -window.panelMargin
                                    Layout.preferredHeight: 32
                                    spacing: 10

                                    RowLayout {
                                        spacing: 0

                                        TabButton {
                                            text: qsTr("Message")
                                            selected: ExampleState.captureMode === 0
                                            onClicked: ExampleState.captureMode = 0
                                        }

                                        TabButton {
                                            text: qsTr("Exception")
                                            selected: ExampleState.captureMode === 1
                                            onClicked: ExampleState.captureMode = 1
                                        }

                                        TabButton {
                                            text: qsTr("Breadcrumb")
                                            selected: ExampleState.captureMode === 2
                                            onClicked: ExampleState.captureMode = 2
                                        }
                                    }

                                    Item {
                                        Layout.fillWidth: true
                                    }

                                    LevelComboBox {
                                        id: captureLevelCombo

                                        currentIndex: ExampleState.captureLevelIndex
                                        onActivated: ExampleState.captureLevelIndex = currentIndex
                                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                                        Layout.rightMargin: window.panelMargin
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10

                                    CaptureOptionComboBox {
                                        id: exceptionKindCombo

                                        visible: ExampleState.captureMode === 1
                                        model: [qsTr("C++"), qsTr("QML")]
                                        widthText: qsTr("QML")
                                        disabledIndexes: [0]
                                        currentIndex: ExampleState.exceptionKindIndex
                                        Component.onCompleted: {
                                            if (ExampleState.exceptionKindIndex === 0)
                                                ExampleState.exceptionKindIndex = 1;
                                        }
                                        onActivated: {
                                            if (!indexEnabled(currentIndex)) {
                                                currentIndex = ExampleState.exceptionKindIndex;
                                            } else {
                                                ExampleState.exceptionKindIndex = currentIndex;
                                            }
                                        }
                                        Layout.preferredWidth: visible ? captureLayout.optionControlWidth : 0
                                        Layout.minimumWidth: visible ? captureLayout.optionControlWidth : 0
                                    }

                                    CaptureOptionComboBox {
                                        id: breadcrumbCategoryCombo

                                        visible: ExampleState.captureMode === 2
                                        model: ["default", "debug", "info", "navigation", "http", "query", "transaction", "ui", "user", "error"]
                                        widthText: qsTr("transaction")
                                        currentIndex: ExampleState.breadcrumbCategoryIndex
                                        onActivated: ExampleState.breadcrumbCategoryIndex = currentIndex
                                        Layout.preferredWidth: visible ? captureLayout.optionControlWidth : 0
                                        Layout.minimumWidth: visible ? captureLayout.optionControlWidth : 0
                                    }

                                    TextField {
                                        text: ExampleState.messageText
                                        onTextEdited: ExampleState.messageText = text
                                        placeholderText: qsTr("Message")
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 80
                                    }

                                    ActionButton {
                                        id: captureButton

                                        text: ExampleState.captureMode === 2 ? qsTr("Add") : qsTr("Capture")
                                        primary: true
                                        Layout.alignment: Qt.AlignRight
                                        Layout.preferredWidth: captureLayout.sideControlWidth
                                        Layout.minimumWidth: captureLayout.sideControlWidth

                                        onClicked: {
                                            capture();
                                        }
                                    }
                                }
                            }
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: window.compact ? 1 : 2
                            rowSpacing: 16
                            columnSpacing: 16

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                implicitHeight: scopeLayout.implicitHeight + window.panelMargin
                                color: theme.surface
                                radius: 8

                                ColumnLayout {
                                    id: scopeLayout

                                    anchors.fill: parent
                                    anchors.margins: window.panelMargin
                                    anchors.topMargin: 0
                                    spacing: 12

                                    RowLayout {
                                        Layout.fillWidth: true
                                        Layout.leftMargin: -window.panelMargin
                                        Layout.rightMargin: -window.panelMargin
                                        spacing: 10

                                        RowLayout {
                                            spacing: 0

                                            TabButton {
                                                text: qsTr("Tags")
                                                selected: ExampleState.scopeTab === 0
                                                onClicked: ExampleState.scopeTab = 0
                                            }

                                            TabButton {
                                                text: qsTr("Contexts")
                                                selected: ExampleState.scopeTab === 1
                                                onClicked: ExampleState.scopeTab = 1
                                            }

                                            TabButton {
                                                text: qsTr("Attachments")
                                                selected: ExampleState.scopeTab === 2
                                                onClicked: ExampleState.scopeTab = 2
                                            }
                                        }

                                        Item {
                                            Layout.fillWidth: true
                                        }

                                        IconToolButton {
                                            text: "+"
                                            Accessible.name: ExampleState.scopeTab === 0 ? qsTr("Add tag") : ExampleState.scopeTab === 1 ? qsTr("Add context") : qsTr("Add attachment")
                                            enabled: Sentry.initialized
                                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                                            Layout.rightMargin: window.panelMargin

                                            onClicked: {
                                                if (ExampleState.scopeTab === 2)
                                                    attachmentFileDialog.open();
                                                else
                                                    scopeEditorPopup.openFor(ExampleState.scopeTab);
                                            }
                                        }
                                    }

                                    StackLayout {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        currentIndex: Math.min(ExampleState.scopeTab, 2)

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            Layout.fillHeight: true
                                            spacing: 10

                                            ScopeEntriesView {
                                                model: tagEntries
                                                scopeTab: 0
                                            }
                                        }

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            Layout.fillHeight: true
                                            spacing: 10

                                            ScopeEntriesView {
                                                model: contextEntries
                                                scopeTab: 1
                                            }
                                        }

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            Layout.fillHeight: true
                                            spacing: 10

                                            AttachmentEntriesView {
                                                model: attachmentEntries
                                            }
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: userLayout.implicitHeight + window.panelMargin
                                color: theme.surface
                                radius: 8

                                ColumnLayout {
                                    id: userLayout

                                    anchors.fill: parent
                                    anchors.margins: window.panelMargin
                                    anchors.topMargin: 0
                                    spacing: 12

                                    Label {
                                        text: qsTr("USER")
                                        color: theme.text
                                        font.pixelSize: 16
                                        font.weight: Font.DemiBold
                                        verticalAlignment: Text.AlignVCenter
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 32
                                    }

                                    GridLayout {
                                        Layout.fillWidth: true
                                        columns: window.compact ? 1 : 2
                                        uniformCellWidths: true
                                        rowSpacing: 10
                                        columnSpacing: 10

                                        LabeledTextField {
                                            label: qsTr("ID")
                                            text: ExampleState.userId
                                            placeholderText: qsTr("12345")
                                            onTextEdited: ExampleState.userId = text
                                            onEditingFinished: syncUser()
                                        }

                                        LabeledTextField {
                                            label: qsTr("Username")
                                            text: ExampleState.username
                                            placeholderText: qsTr("jane")
                                            onTextEdited: ExampleState.username = text
                                            onEditingFinished: syncUser()
                                        }

                                        LabeledTextField {
                                            label: qsTr("Email")
                                            text: ExampleState.email
                                            placeholderText: qsTr("jane@example.com")
                                            onTextEdited: ExampleState.email = text
                                            onEditingFinished: syncUser()
                                        }

                                        LabeledTextField {
                                            label: qsTr("IP address")
                                            text: ExampleState.ipAddress
                                            placeholderText: qsTr("127.0.0.1")
                                            onTextEdited: ExampleState.ipAddress = text
                                            onEditingFinished: syncUser()
                                        }
                                    }
                                }
                            }
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: window.compact ? 1 : 2
                            rowSpacing: 16
                            columnSpacing: 16

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.row: window.compact ? 1 : 0
                                Layout.column: window.compact ? 0 : 1
                                implicitHeight: sessionLayout.implicitHeight + window.panelMargin
                                color: theme.surface
                                radius: 8

                                ColumnLayout {
                                    id: sessionLayout

                                    anchors.fill: parent
                                    anchors.margins: window.panelMargin
                                    anchors.topMargin: 0
                                    spacing: 12

                                    RowLayout {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 32
                                        spacing: 8

                                        Label {
                                            text: qsTr("SESSION")
                                            color: theme.text
                                            font.pixelSize: 16
                                            font.weight: Font.DemiBold
                                            verticalAlignment: Text.AlignVCenter
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 32
                                        }

                                        IconToolButton {
                                            text: ExampleState.sessionActive ? "\u25a0" : "\u25b6"
                                            Accessible.name: ExampleState.sessionActive ? qsTr("End session") : qsTr("Start session")
                                            destructive: ExampleState.sessionActive
                                            quietDestructive: ExampleState.sessionActive
                                            tooltip: ExampleState.sessionActive ? qsTr("End session") : qsTr("Start session")
                                            enabled: Sentry.initialized

                                            onClicked: {
                                                toggleSession();
                                            }
                                        }
                                    }

                                    GridLayout {
                                        Layout.fillWidth: true
                                        columns: window.compact ? 1 : 2
                                        uniformCellWidths: true
                                        rowSpacing: 10
                                        columnSpacing: 10

                                        LabeledTextField {
                                            label: qsTr("Release")
                                            text: ExampleState.sessionRelease
                                            placeholderText: qsTr("my-app@1.0.0")
                                            onTextEdited: ExampleState.sessionRelease = text
                                        }

                                        LabeledTextField {
                                            label: qsTr("Environment")
                                            text: ExampleState.sessionEnvironment
                                            placeholderText: qsTr("production")
                                            onTextEdited: ExampleState.sessionEnvironment = text
                                        }
                                    }

                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.row: 0
                                Layout.column: 0
                                implicitHeight: crashLayout.implicitHeight + window.panelMargin
                                color: theme.surface
                                radius: 8

                                ColumnLayout {
                                    id: crashLayout

                                    anchors.fill: parent
                                    anchors.margins: window.panelMargin
                                    anchors.topMargin: 0
                                    spacing: 12

                                    RowLayout {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 32

                                        Label {
                                            text: qsTr("CRASH")
                                            color: theme.text
                                            font.pixelSize: 16
                                            font.weight: Font.DemiBold
                                            verticalAlignment: Text.AlignVCenter
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 32
                                        }
                                    }

                                    GridLayout {
                                        Layout.fillWidth: true
                                        columns: 2
                                        rowSpacing: 10
                                        columnSpacing: 10

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 80
                                            spacing: 6

                                            Label {
                                                text: qsTr("Type")
                                                color: theme.muted
                                                font.pixelSize: 12
                                                font.weight: Font.DemiBold
                                                Layout.fillWidth: true
                                            }

                                            ComboBox {
                                                id: crashKindCombo

                                                model: [qsTr("Native"), qsTr("QML")]
                                                currentIndex: ExampleState.crashKindIndex
                                                onActivated: ExampleState.crashKindIndex = currentIndex
                                                font.pixelSize: 14
                                                implicitHeight: window.controlHeight
                                                leftPadding: 12
                                                rightPadding: 30
                                                Layout.fillWidth: true
                                                contentItem: Text {
                                                    text: crashKindCombo.displayText
                                                    color: theme.text
                                                    font: crashKindCombo.font
                                                    verticalAlignment: Text.AlignVCenter
                                                    elide: Text.ElideRight
                                                    leftPadding: crashKindCombo.leftPadding
                                                    rightPadding: crashKindCombo.rightPadding
                                                }
                                                indicator: ComboChevron {
                                                    x: crashKindCombo.width - width - 12
                                                    y: (crashKindCombo.height - height) / 2
                                                    strokeColor: crashKindCombo.hovered || crashKindCombo.popup.visible ? theme.text : theme.muted
                                                }
                                                delegate: ItemDelegate {
                                                    id: crashKindDelegate

                                                    required property string modelData
                                                    required property int index

                                                    width: crashKindCombo.width
                                                    height: 38
                                                    hoverEnabled: true
                                                    highlighted: crashKindCombo.highlightedIndex === index
                                                    contentItem: Text {
                                                        text: crashKindDelegate.modelData
                                                        color: crashKindDelegate.highlighted || crashKindCombo.currentIndex === crashKindDelegate.index ? theme.text : theme.muted
                                                        font: crashKindCombo.font
                                                        verticalAlignment: Text.AlignVCenter
                                                        elide: Text.ElideRight
                                                    }
                                                    background: Rectangle {
                                                        color: crashKindDelegate.highlighted ? "#343440" : crashKindCombo.currentIndex === crashKindDelegate.index ? "#2d2d33" : "transparent"
                                                    }
                                                }
                                                background: Rectangle {
                                                    color: crashKindCombo.hovered ? theme.inputFocus : theme.input
                                                    border.color: theme.border
                                                    radius: 7
                                                }
                                                popup: Popup {
                                                    y: crashKindCombo.height + 4
                                                    width: crashKindCombo.width
                                                    implicitHeight: contentItem.implicitHeight + 8
                                                    padding: 4

                                                    contentItem: ListView {
                                                        clip: true
                                                        implicitHeight: contentHeight
                                                        model: crashKindCombo.popup.visible ? crashKindCombo.delegateModel : null
                                                        currentIndex: crashKindCombo.highlightedIndex
                                                    }
                                                    background: Rectangle {
                                                        color: theme.input
                                                        border.color: theme.border
                                                        radius: 7
                                                    }
                                                }
                                            }
                                        }

                                        ActionButton {
                                            text: qsTr("Crash")
                                            destructive: true
                                            enabled: Sentry.initialized
                                            Layout.alignment: Qt.AlignBottom | Qt.AlignRight
                                            Layout.preferredWidth: window.actionWidth
                                            Layout.minimumWidth: window.actionWidth

                                            onClicked: {
                                                triggerCrash();
                                            }
                                        }
                                    }

                                }
                            }
                        }
                    }
                }

                FloatingActionButton {
                    id: feedbackButton

                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: window.pageMargin
                    tooltip: qsTr("Feedback")
                    iconComponent: Component {
                        EnvelopeIcon {
                            strokeColor: feedbackButton.enabled ? theme.text : theme.disabledText
                        }
                    }

                    onClicked: {
                        feedbackPopup.openFeedback();
                    }
                }
            }
        }
}
