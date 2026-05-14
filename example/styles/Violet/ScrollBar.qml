import QtQuick
import QtQuick.Templates as T

T.ScrollBar {
    id: control

    implicitWidth: control.interactive ? 10 : 4
    implicitHeight: control.interactive ? 10 : 4
    padding: 2
    visible: control.policy !== T.ScrollBar.AlwaysOff
    minimumSize: orientation === Qt.Horizontal ? height / width : width / height

    contentItem: Rectangle {
        implicitWidth: 4
        implicitHeight: 4
        radius: width / 2
        color: Theme.border
        opacity: control.pressed ? 0.9 : control.hovered ? 0.7 : 0.45
    }
}
