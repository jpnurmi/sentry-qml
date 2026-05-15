import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Violet as S

ColumnLayout {
    id: control

    property string label: ""
    property alias text: textField.text
    property alias placeholderText: textField.placeholderText
    property alias inputMethodHints: textField.inputMethodHints
    property alias readOnly: textField.readOnly
    property alias validator: textField.validator
    property string trailingActionText: ""
    property string trailingActionAccessibleName: trailingActionText
    property string trailingActionTooltip: trailingActionAccessibleName
    property bool trailingActionEnabled: true

    signal accepted()
    signal textEdited()
    signal editingFinished()
    signal trailingActionTriggered()

    function accept() {
        accepted();
    }

    function triggerTrailingAction() {
        if (trailingActionButton.visible && trailingActionButton.enabled)
            trailingActionTriggered();
    }

    spacing: labelItem.visible ? 6 : 0
    Layout.fillWidth: true

    Label {
        id: labelItem

        text: control.label
        visible: text.length > 0
        color: S.Theme.muted
        font.pixelSize: 12
        font.weight: Font.DemiBold
        Layout.fillWidth: true
    }

    Item {
        id: fieldContainer

        Layout.fillWidth: true
        implicitWidth: textField.implicitWidth
        implicitHeight: textField.implicitHeight

        TextField {
            id: textField

            anchors.fill: parent
            font.pixelSize: 15
            rightPadding: trailingActionButton.visible ? trailingActionButton.width + 22 : 14

            onAccepted: control.accept()
            onTextEdited: control.textEdited()
            onEditingFinished: control.editingFinished()
        }

        ToolButton {
            id: trailingActionButton

            readonly property bool actionHighlighted: hovered || down || visualFocus

            visible: control.trailingActionText.length > 0
            enabled: control.trailingActionEnabled
            text: control.trailingActionText
            Accessible.name: control.trailingActionAccessibleName
            hoverEnabled: true
            implicitWidth: 28
            implicitHeight: 28
            width: implicitWidth
            height: implicitHeight
            padding: 0
            anchors.right: parent.right
            anchors.rightMargin: 7
            anchors.verticalCenter: parent.verticalCenter
            z: 1

            contentItem: Text {
                text: trailingActionButton.text
                color: {
                    if (!trailingActionButton.enabled)
                        return S.Theme.disabledText;
                    return trailingActionButton.actionHighlighted ? S.Theme.text : S.Theme.muted;
                }
                font.pixelSize: 16
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }

            background: Rectangle {
                radius: width / 2
                color: {
                    if (!trailingActionButton.enabled)
                        return "transparent";
                    if (trailingActionButton.down)
                        return S.Theme.surfaceRaised;
                    return trailingActionButton.hovered || trailingActionButton.visualFocus ? "#24242b" : "transparent";
                }
            }

            ToolTip {
                text: control.trailingActionTooltip
                visible: control.trailingActionTooltip.length > 0 && trailingActionButton.hovered
                x: Math.round((trailingActionButton.width - width) / 2)
                y: -height - 8
            }

            onClicked: control.triggerTrailingAction()
        }
    }
}
