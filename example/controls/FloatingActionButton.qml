import QtQuick
import QtQuick.Controls
import Violet as S

ToolButton {
    id: control

    property string tooltip: ""
    property url iconSource
    property color backgroundColor: S.Theme.accent
    property color hoverColor: S.Theme.accentHover
    property color pressedColor: S.Theme.accentPressed
    readonly property bool hasIcon: iconSource.toString().length > 0

    Accessible.name: tooltip.length > 0 ? tooltip : text
    hoverEnabled: true
    implicitWidth: 48
    implicitHeight: 48
    padding: 0
    z: 10

    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            visible: control.hasIcon
            source: control.iconSource
            width: control.icon.width > 0 ? control.icon.width : 22
            height: control.icon.height > 0 ? control.icon.height : 22
            opacity: control.enabled ? 1 : 0.5
        }

        Text {
            anchors.centerIn: parent
            visible: !control.hasIcon
            text: control.text
            color: control.enabled ? S.Theme.text : S.Theme.disabledText
            font: control.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    background: Rectangle {
        radius: width / 2
        color: !control.enabled ? S.Theme.disabled : control.down ? control.pressedColor : control.hovered ? control.hoverColor : control.backgroundColor
    }

    ToolTip {
        text: control.tooltip
        visible: control.hovered && control.tooltip.length > 0
        x: Math.round((control.width - width) / 2)
        y: -height - 8
    }
}
