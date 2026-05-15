import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import "controls"

Popup {
    id: root

    property int messageHeight: 160
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

    modal: true
    focus: true
    padding: AppTheme.panelMargin

    contentItem: ColumnLayout {
        spacing: AppTheme.groupSpacing

        Label {
            text: qsTr("Feedback")
            color: AppTheme.text
            font.pixelSize: 16
            font.weight: Font.DemiBold
            Layout.fillWidth: true
        }

        GridLayout {
            Layout.fillWidth: true
            columns: AppTheme.compact ? 1 : 2
            uniformCellWidths: true
            rowSpacing: AppTheme.formSpacing
            columnSpacing: AppTheme.formSpacing

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
            spacing: AppTheme.labelSpacing

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
                topPadding: AppTheme.formSpacing
                bottomPadding: AppTheme.formSpacing
                Layout.fillWidth: true
                Layout.preferredHeight: root.messageHeight

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
            spacing: AppTheme.formSpacing

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
