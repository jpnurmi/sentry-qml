import QtQuick
import QtQuick.Controls
import Violet as S

ToolButton {
    id: control

    text: "\u2039"
    Accessible.name: qsTr("Back")
    hoverEnabled: true
    implicitWidth: 40
    implicitHeight: 40
    padding: 0

    contentItem: Item {
        Text {
            anchors.centerIn: parent
            anchors.horizontalCenterOffset: 1
            anchors.verticalCenterOffset: -1
            text: control.text
            color: control.enabled ? S.Theme.text : S.Theme.disabledText
            font.pixelSize: 32
            font.weight: Font.DemiBold
        }
    }

    background: Rectangle {
        radius: 8
        color: control.down ? S.Theme.surfaceRaised : control.hovered ? "#24242b" : "transparent"
    }
}
