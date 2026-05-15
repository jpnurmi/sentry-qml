import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import "controls"

Popup {
    id: root

    property bool compact: false
    property real pageMargin: 20
    property real panelMargin: 18
    property var sendFeedback: null

    function openFeedback() {
        feedbackNameField.text = "";
        feedbackEmailField.text = AppState.email;
        feedbackMessageArea.text = "";
        open();
    }

    function submitFeedback() {
        if (root.sendFeedback && root.sendFeedback(feedbackNameField.text, feedbackEmailField.text, feedbackMessageArea.text))
            close();
    }

    x: (Window.width - width) / 2
    y: Math.max(root.pageMargin, (Window.height - height) / 2)
    width: Math.min(Math.max(0, Window.width - root.pageMargin * 2), 520)
    modal: true
    focus: true
    padding: root.panelMargin
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    contentItem: ColumnLayout {
        spacing: 14

        Label {
            text: qsTr("Feedback")
            color: AppTheme.text
            font.pixelSize: 16
            font.weight: Font.DemiBold
            Layout.fillWidth: true
        }

        GridLayout {
            Layout.fillWidth: true
            columns: root.compact ? 1 : 2
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
                color: AppTheme.muted
                font.pixelSize: 12
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }

            TextArea {
                id: feedbackMessageArea

                placeholderText: qsTr("What happened?")
                placeholderTextColor: AppTheme.subtle
                color: AppTheme.text
                selectedTextColor: AppTheme.text
                selectionColor: AppTheme.accent
                font.pixelSize: 15
                wrapMode: TextArea.Wrap
                leftPadding: 12
                rightPadding: 12
                topPadding: 10
                bottomPadding: 10
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(220, Math.max(130, Window.height * 0.24))

                background: Rectangle {
                    color: feedbackMessageArea.activeFocus ? AppTheme.inputFocus : AppTheme.input
                    border.color: feedbackMessageArea.activeFocus ? AppTheme.accent : AppTheme.border
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
                    root.close();
                }
            }

            ActionButton {
                text: qsTr("Send")
                primary: true
                enabled: feedbackMessageArea.text.trim().length > 0

                onClicked: {
                    root.submitFeedback();
                }
            }
        }
    }

    background: Rectangle {
        color: AppTheme.surface
        border.color: AppTheme.border
        radius: 8
    }
}
