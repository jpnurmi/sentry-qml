import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Sentry 1.0

import "controls"

Item {
    id: root

    property string consentText: ""
    property color consentColor: AppTheme.success
    property var tagModel
    property var contextModel
    property var attachmentModel
    readonly property int pageMargin: AppTheme.pageMargin
    readonly property int panelMargin: AppTheme.panelMargin
    readonly property int controlHeight: AppTheme.controlHeight
    readonly property int actionWidth: Math.max(152, Math.ceil(Math.max(
        giveActionMetrics.width,
        revokeActionMetrics.width,
        crashActionMetrics.width
    ) + 28))

    signal backRequested()
    signal feedbackRequested()
    signal captureRequested()
    signal addScopeItemRequested(var tab)
    signal addAttachmentRequested()
    signal syncUserRequested()
    signal toggleSessionRequested()
    signal triggerCrashRequested()
    signal toggleUserConsentRequested()
    signal removeScopeEntryRequested(var scopeTab, var index, var key, var entryModel)
    signal removeAttachmentRequested(var index)

    TextMetrics {
        id: giveActionMetrics

        font.pixelSize: 14
        font.weight: Font.DemiBold
        text: qsTr("Give")
    }

    TextMetrics {
        id: revokeActionMetrics

        font.pixelSize: 14
        font.weight: Font.DemiBold
        text: qsTr("Revoke")
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
        readonly property color actionColor: giveConsent ? AppTheme.success : AppTheme.critical

        text: giveConsent ? qsTr("Give") : qsTr("Revoke")
        hoverEnabled: true
        font.pixelSize: 14
        font.weight: Font.DemiBold
        implicitWidth: root.actionWidth
        implicitHeight: root.controlHeight
        contentItem: Text {
            text: consentButton.text
            color: consentButton.enabled ? AppTheme.text : AppTheme.disabledText
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
                    return AppTheme.disabled;
                if (consentButton.down)
                    return Qt.darker(consentButton.actionColor, 1.12);
                if (consentButton.hovered)
                    return Qt.lighter(consentButton.actionColor, 1.08);
                return consentButton.actionColor;
            }
        }
    }

    component ConsentStatusIcon: Icon {
        id: consentIcon

        property int consent: Sentry.userConsent
        readonly property bool given: consent === Sentry.UserConsentGiven
        readonly property bool revoked: consent === Sentry.UserConsentRevoked

        source: given ? "qrc:/images/consent-given.svg"
            : revoked ? "qrc:/images/consent-revoked.svg" : "qrc:/images/consent-unknown.svg"
        implicitWidth: 28
        implicitHeight: 28
    }

    component ConsentPanel: Rectangle {
        id: consentPanel

        readonly property bool consentActionable: Sentry.userConsentRequired

        implicitHeight: consentPanelLayout.implicitHeight + root.panelMargin
        radius: 8
        color: AppTheme.surface

        ColumnLayout {
            id: consentPanelLayout

            anchors.fill: parent
            anchors.margins: root.panelMargin
            anchors.topMargin: 0
            spacing: AppTheme.panelSpacing

            Label {
                text: qsTr("CONSENT")
                color: AppTheme.text
                font.pixelSize: 16
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
                Layout.fillWidth: true
                Layout.preferredHeight: 32
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: AppTheme.formSpacing

                ConsentStatusIcon {
                    Layout.alignment: Qt.AlignVCenter
                    consent: Sentry.userConsent
                }

                Text {
                    text: root.consentText
                    color: consentPanel.consentActionable ? root.consentColor : AppTheme.muted
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                ConsentActionButton {
                    giveConsent: Sentry.userConsent !== Sentry.UserConsentGiven
                    enabled: consentPanel.consentActionable
                    Layout.preferredWidth: root.actionWidth

                    onClicked: {
                        root.toggleUserConsentRequested();
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
        leftPadding: root.panelMargin
        rightPadding: root.panelMargin
        topPadding: 0
        bottomPadding: 0
        implicitWidth: tabText.implicitWidth + leftPadding + rightPadding
        implicitHeight: 32
        contentItem: Text {
            id: tabText

            text: tabButton.text.toUpperCase()
            color: tabButton.selected ? AppTheme.text : AppTheme.subtle
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
                color: AppTheme.accent
                opacity: tabButton.selected ? 1 : tabButton.hovered ? 0.28 : 0
            }
        }
    }

    component LevelComboBox: ComboBox {
        id: combo

        property string levelValue: currentText.toLowerCase()

        function levelColor(index) {
            if (index === 1)
                return AppTheme.warning;
            if (index === 2)
                return AppTheme.critical;
            return AppTheme.info;
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
                color: AppTheme.text
                font: combo.font
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
        indicator: ComboChevron {
            x: combo.width - width - 12
            y: (combo.height - height) / 2
            active: combo.hovered || combo.popup.visible
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
                    color: comboDelegate.highlighted || combo.currentIndex === comboDelegate.index ? AppTheme.text : AppTheme.muted
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
                color: AppTheme.input
                border.color: AppTheme.border
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
        implicitHeight: root.controlHeight
        leftPadding: 12
        rightPadding: 30
        Layout.fillWidth: false
        contentItem: Text {
            text: optionCombo.displayText
            color: AppTheme.text
            font: optionCombo.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        indicator: ComboChevron {
            x: optionCombo.width - width - 12
            y: (optionCombo.height - height) / 2
            active: optionCombo.hovered || optionCombo.popup.visible
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
                color: !optionDelegate.enabled ? AppTheme.disabledText : optionDelegate.highlighted || optionCombo.currentIndex === optionDelegate.index ? AppTheme.text : AppTheme.muted
                font: optionCombo.font
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            background: Rectangle {
                color: !optionDelegate.enabled ? "transparent" : optionDelegate.highlighted ? "#343440" : optionCombo.currentIndex === optionDelegate.index ? "#2d2d33" : "transparent"
            }
        }
        background: Rectangle {
            color: optionCombo.hovered ? AppTheme.inputFocus : AppTheme.input
            border.color: AppTheme.border
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
                color: AppTheme.input
                border.color: AppTheme.border
                radius: 7
            }
        }

        TextMetrics {
            id: optionComboMetrics

            font: optionCombo.font
            text: optionCombo.widthText
        }
    }

    component ScopeEntriesView: Item {
        id: entriesView

        property var model
        property int scopeTab: 0
        readonly property int headerHeight: 34
        readonly property int rowHeight: 34
        readonly property int tablePadding: AppTheme.panelMargin
        readonly property int removeColumnWidth: 40
        readonly property int keyColumnWidth: Math.round((tableFrame.width - tablePadding * 2 - removeColumnWidth) * 0.42)
        readonly property int valueColumnX: tablePadding + keyColumnWidth

        function removeEntry(index, key) {
            root.removeScopeEntryRequested(scopeTab, index, key, model);
        }

        Layout.fillWidth: true
        Layout.fillHeight: true
        implicitHeight: tableFrame.implicitHeight

        Rectangle {
            id: tableFrame

            anchors.fill: parent
            implicitHeight: AppTheme.compact ? 148 : 124
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
                        color: AppTheme.text
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
                        color: AppTheme.text
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
                            color: AppTheme.text
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
                            color: AppTheme.text
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

    component AttachmentEntriesView: Item {
        id: attachmentsView

        property var model
        readonly property int headerHeight: 34
        readonly property int rowHeight: 34
        readonly property int tablePadding: AppTheme.panelMargin
        readonly property int removeColumnWidth: 40
        readonly property int sizeColumnWidth: Math.min(120, Math.max(86, Math.round(tableFrame.width * 0.18)))
        readonly property int sizeColumnX: tableFrame.width - tablePadding - removeColumnWidth - sizeColumnWidth
        readonly property int fileColumnWidth: Math.max(80, sizeColumnX - tablePadding - 12)

        Layout.fillWidth: true
        Layout.fillHeight: true
        implicitHeight: tableFrame.implicitHeight

        Rectangle {
            id: tableFrame

            anchors.fill: parent
            implicitHeight: AppTheme.compact ? 148 : 124
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
                        color: AppTheme.text
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
                        color: AppTheme.text
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
                            color: AppTheme.text
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
                            color: AppTheme.text
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
                                root.removeAttachmentRequested(attachmentRow.index);
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

    ScrollView {
        id: runtimeScrollView

        anchors.fill: parent
        clip: true
        padding: root.pageMargin
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical.policy: ScrollBar.AsNeeded

        ColumnLayout {
            id: runtimePage

            width: runtimeScrollView.availableWidth
            spacing: AppTheme.pageSpacing

            PageHeader {
                canGoBack: true

                onBackClicked: root.backRequested()
            }

            ConsentPanel {
                visible: AppState.requireUserConsentEnabled && Sentry.initialized
                Layout.fillWidth: true
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: captureLayout.implicitHeight + root.panelMargin
                color: AppTheme.surface
                radius: 8

                ColumnLayout {
                    id: captureLayout

                    readonly property real optionControlWidth: Math.max(exceptionKindCombo.implicitWidth, breadcrumbCategoryCombo.implicitWidth)

                    anchors.fill: parent
                    anchors.margins: root.panelMargin
                    anchors.topMargin: 0
                    spacing: AppTheme.panelSpacing

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: -root.panelMargin
                        Layout.rightMargin: -root.panelMargin
                        Layout.preferredHeight: 32
                        spacing: AppTheme.formSpacing

                        RowLayout {
                            spacing: 0

                            TabButton {
                                text: qsTr("Message")
                                selected: AppState.captureMode === 0
                                onClicked: AppState.captureMode = 0
                            }

                            TabButton {
                                text: qsTr("Exception")
                                selected: AppState.captureMode === 1
                                onClicked: AppState.captureMode = 1
                            }

                            TabButton {
                                text: qsTr("Breadcrumb")
                                selected: AppState.captureMode === 2
                                onClicked: AppState.captureMode = 2
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                        }

                        LevelComboBox {
                            id: captureLevelCombo

                            currentIndex: AppState.captureLevelIndex
                            onActivated: AppState.captureLevelIndex = currentIndex
                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                            Layout.rightMargin: root.panelMargin
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: AppTheme.formSpacing

                        CaptureOptionComboBox {
                            id: exceptionKindCombo

                            visible: AppState.captureMode === 1
                            model: [qsTr("C++"), qsTr("QML")]
                            widthText: qsTr("QML")
                            disabledIndexes: [0]
                            currentIndex: AppState.exceptionKindIndex
                            Component.onCompleted: {
                                if (AppState.exceptionKindIndex === 0)
                                    AppState.exceptionKindIndex = 1;
                            }
                            onActivated: {
                                if (!indexEnabled(currentIndex)) {
                                    currentIndex = AppState.exceptionKindIndex;
                                } else {
                                    AppState.exceptionKindIndex = currentIndex;
                                }
                            }
                            Layout.preferredWidth: visible ? captureLayout.optionControlWidth : 0
                            Layout.minimumWidth: visible ? captureLayout.optionControlWidth : 0
                        }

                        CaptureOptionComboBox {
                            id: breadcrumbCategoryCombo

                            visible: AppState.captureMode === 2
                            model: ["default", "debug", "info", "navigation", "http", "query", "transaction", "ui", "user", "error"]
                            widthText: qsTr("transaction")
                            currentIndex: AppState.breadcrumbCategoryIndex
                            onActivated: AppState.breadcrumbCategoryIndex = currentIndex
                            Layout.preferredWidth: visible ? captureLayout.optionControlWidth : 0
                            Layout.minimumWidth: visible ? captureLayout.optionControlWidth : 0
                        }

                        LabeledTextField {
                            id: messageField

                            text: AppState.messageText
                            placeholderText: qsTr("Message")
                            trailingActionText: "\u2192"
                            trailingActionAccessibleName: AppState.captureMode === 2 ? qsTr("Add") : qsTr("Capture")
                            trailingActionTooltip: messageField.trailingActionAccessibleName
                            trailingActionEnabled: AppState.messageText.trim().length > 0
                            Layout.fillWidth: true
                            Layout.minimumWidth: 80

                            onTextEdited: AppState.messageText = text
                            onAccepted: {
                                if (!messageField.trailingActionEnabled)
                                    return;
                                root.captureRequested();
                                AppState.messageText = "";
                            }
                            onTrailingActionTriggered: root.captureRequested()
                        }
                    }
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: AppTheme.compact ? 1 : 2
                rowSpacing: AppTheme.pageSpacing
                columnSpacing: AppTheme.pageSpacing

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    implicitHeight: scopeLayout.implicitHeight + root.panelMargin
                    color: AppTheme.surface
                    radius: 8

                    ColumnLayout {
                        id: scopeLayout

                        anchors.fill: parent
                        anchors.margins: root.panelMargin
                        anchors.topMargin: 0
                        spacing: AppTheme.panelSpacing

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.leftMargin: -root.panelMargin
                            Layout.rightMargin: -root.panelMargin
                            spacing: AppTheme.formSpacing

                            RowLayout {
                                spacing: 0

                                TabButton {
                                    text: qsTr("Tags")
                                    selected: AppState.scopeTab === 0
                                    onClicked: AppState.scopeTab = 0
                                }

                                TabButton {
                                    text: qsTr("Contexts")
                                    selected: AppState.scopeTab === 1
                                    onClicked: AppState.scopeTab = 1
                                }

                                TabButton {
                                    text: qsTr("Attachments")
                                    selected: AppState.scopeTab === 2
                                    onClicked: AppState.scopeTab = 2
                                }
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            IconToolButton {
                                text: "+"
                                Accessible.name: AppState.scopeTab === 0 ? qsTr("Add tag") : AppState.scopeTab === 1 ? qsTr("Add context") : qsTr("Add attachment")
                                enabled: Sentry.initialized
                                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                                Layout.rightMargin: root.panelMargin

                                onClicked: {
                                    if (AppState.scopeTab === 2)
                                        root.addAttachmentRequested();
                                    else
                                        root.addScopeItemRequested(AppState.scopeTab);
                                }
                            }
                        }

                        StackLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            currentIndex: Math.min(AppState.scopeTab, 2)

                            ScopeEntriesView {
                                model: root.tagModel
                                scopeTab: 0
                            }

                            ScopeEntriesView {
                                model: root.contextModel
                                scopeTab: 1
                            }

                            AttachmentEntriesView {
                                model: root.attachmentModel
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: userLayout.implicitHeight + root.panelMargin
                    color: AppTheme.surface
                    radius: 8

                    ColumnLayout {
                        id: userLayout

                        anchors.fill: parent
                        anchors.margins: root.panelMargin
                        anchors.topMargin: 0
                        spacing: 12

                        Label {
                            text: qsTr("USER")
                            color: AppTheme.text
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                            verticalAlignment: Text.AlignVCenter
                            Layout.fillWidth: true
                            Layout.preferredHeight: 32
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: AppTheme.compact ? 1 : 2
                            uniformCellWidths: true
                            rowSpacing: AppTheme.formSpacing
                            columnSpacing: AppTheme.formSpacing

                            LabeledTextField {
                                label: qsTr("ID")
                                text: AppState.userId
                                placeholderText: qsTr("12345")
                                onTextEdited: AppState.userId = text
                                onEditingFinished: root.syncUserRequested()
                            }

                            LabeledTextField {
                                label: qsTr("Username")
                                text: AppState.username
                                placeholderText: qsTr("jane")
                                onTextEdited: AppState.username = text
                                onEditingFinished: root.syncUserRequested()
                            }

                            LabeledTextField {
                                label: qsTr("Email")
                                text: AppState.email
                                placeholderText: qsTr("jane@example.com")
                                onTextEdited: AppState.email = text
                                onEditingFinished: root.syncUserRequested()
                            }

                            LabeledTextField {
                                label: qsTr("IP address")
                                text: AppState.ipAddress
                                placeholderText: qsTr("127.0.0.1")
                                onTextEdited: AppState.ipAddress = text
                                onEditingFinished: root.syncUserRequested()
                            }
                        }
                    }
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: AppTheme.compact ? 1 : 2
                rowSpacing: AppTheme.pageSpacing
                columnSpacing: AppTheme.pageSpacing

                Rectangle {
                    Layout.fillWidth: true
                    Layout.row: AppTheme.compact ? 1 : 0
                    Layout.column: AppTheme.compact ? 0 : 1
                    implicitHeight: sessionLayout.implicitHeight + root.panelMargin
                    color: AppTheme.surface
                    radius: 8

                    ColumnLayout {
                        id: sessionLayout

                        anchors.fill: parent
                        anchors.margins: root.panelMargin
                        anchors.topMargin: 0
                        spacing: AppTheme.panelSpacing

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 32
                            spacing: AppTheme.labelSpacing

                            Label {
                                text: qsTr("SESSION")
                                color: AppTheme.text
                                font.pixelSize: 16
                                font.weight: Font.DemiBold
                                verticalAlignment: Text.AlignVCenter
                                Layout.fillWidth: true
                                Layout.preferredHeight: 32
                            }

                            IconToolButton {
                                text: AppState.sessionActive ? "\u25a0" : "\u25b6"
                                Accessible.name: AppState.sessionActive ? qsTr("End session") : qsTr("Start session")
                                destructive: AppState.sessionActive
                                quietDestructive: AppState.sessionActive
                                tooltip: AppState.sessionActive ? qsTr("End session") : qsTr("Start session")
                                enabled: Sentry.initialized

                                onClicked: {
                                    root.toggleSessionRequested();
                                }
                            }
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: AppTheme.compact ? 1 : 2
                            uniformCellWidths: true
                            rowSpacing: AppTheme.formSpacing
                            columnSpacing: AppTheme.formSpacing

                            LabeledTextField {
                                label: qsTr("Release")
                                text: AppState.sessionRelease
                                placeholderText: qsTr("my-app@1.0.0")
                                onTextEdited: AppState.sessionRelease = text
                            }

                            LabeledTextField {
                                label: qsTr("Environment")
                                text: AppState.sessionEnvironment
                                placeholderText: qsTr("production")
                                onTextEdited: AppState.sessionEnvironment = text
                            }
                        }

                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.row: 0
                    Layout.column: 0
                    implicitHeight: crashLayout.implicitHeight + root.panelMargin
                    color: AppTheme.surface
                    radius: 8

                    ColumnLayout {
                        id: crashLayout

                        anchors.fill: parent
                        anchors.margins: root.panelMargin
                        anchors.topMargin: 0
                        spacing: AppTheme.panelSpacing

                        Label {
                            text: qsTr("CRASH")
                            color: AppTheme.text
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                            verticalAlignment: Text.AlignVCenter
                            Layout.fillWidth: true
                            Layout.preferredHeight: 32
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            rowSpacing: AppTheme.formSpacing
                            columnSpacing: AppTheme.formSpacing

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 80
                                spacing: AppTheme.labelSpacing

                                Label {
                                    text: qsTr("Type")
                                    color: AppTheme.muted
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                    Layout.fillWidth: true
                                }

                                ComboBox {
                                    id: crashKindCombo

                                    model: [qsTr("Native"), qsTr("QML")]
                                    currentIndex: AppState.crashKindIndex
                                    onActivated: AppState.crashKindIndex = currentIndex
                                    font.pixelSize: 14
                                    implicitHeight: root.controlHeight
                                    leftPadding: 12
                                    rightPadding: 30
                                    Layout.fillWidth: true
                                    contentItem: Text {
                                        text: crashKindCombo.displayText
                                        color: AppTheme.text
                                        font: crashKindCombo.font
                                        verticalAlignment: Text.AlignVCenter
                                        elide: Text.ElideRight
                                        leftPadding: crashKindCombo.leftPadding
                                        rightPadding: crashKindCombo.rightPadding
                                    }
                                    indicator: ComboChevron {
                                        x: crashKindCombo.width - width - 12
                                        y: (crashKindCombo.height - height) / 2
                                        active: crashKindCombo.hovered || crashKindCombo.popup.visible
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
                                            color: crashKindDelegate.highlighted || crashKindCombo.currentIndex === crashKindDelegate.index ? AppTheme.text : AppTheme.muted
                                            font: crashKindCombo.font
                                            verticalAlignment: Text.AlignVCenter
                                            elide: Text.ElideRight
                                        }
                                        background: Rectangle {
                                            color: crashKindDelegate.highlighted ? "#343440" : crashKindCombo.currentIndex === crashKindDelegate.index ? "#2d2d33" : "transparent"
                                        }
                                    }
                                    background: Rectangle {
                                        color: crashKindCombo.hovered ? AppTheme.inputFocus : AppTheme.input
                                        border.color: AppTheme.border
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
                                            color: AppTheme.input
                                            border.color: AppTheme.border
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
                                Layout.preferredWidth: root.actionWidth
                                Layout.minimumWidth: root.actionWidth

                                onClicked: {
                                    root.triggerCrashRequested();
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
        anchors.margins: root.pageMargin
        tooltip: qsTr("Feedback")
        iconSource: "qrc:/images/feedback.svg"
        icon.width: 22
        icon.height: 16

        onClicked: {
            root.feedbackRequested();
        }
    }
}
