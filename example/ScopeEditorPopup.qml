import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import "controls"

Popup {
    id: root

    property int scopeTab: 0
    property var applyScope: null

    function openFor(tab) {
        root.scopeTab = tab;
        scopeKeyField.text = "";
        scopeValueField.text = "";
        open();
    }

    function submitScope() {
        if (root.scopeTab === 0) {
            AppState.tagKey = scopeKeyField.text;
            AppState.tagValue = scopeValueField.text;
        } else {
            AppState.contextKey = scopeKeyField.text;
            AppState.contextValue = scopeValueField.text;
        }
        AppState.scopeTab = root.scopeTab;
        if (root.applyScope)
            root.applyScope();
        close();
    }

    modal: true
    focus: true
    padding: AppTheme.panelMargin

    contentItem: ColumnLayout {
        spacing: AppTheme.groupSpacing

        Label {
            text: root.scopeTab === 0 ? qsTr("Add tag") : qsTr("Add context")
            color: AppTheme.text
            font.pixelSize: 16
            font.weight: Font.DemiBold
            Layout.fillWidth: true
        }

        LabeledTextField {
            id: scopeKeyField

            label: root.scopeTab === 0 ? qsTr("Key") : qsTr("Context")
            placeholderText: root.scopeTab === 0 ? qsTr("feature") : qsTr("device")
        }

        LabeledTextField {
            id: scopeValueField

            label: qsTr("Value")
            placeholderText: root.scopeTab === 0 ? qsTr("checkout") : qsTr("mobile")
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
                text: qsTr("Add")
                primary: true

                onClicked: {
                    root.submitScope();
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
