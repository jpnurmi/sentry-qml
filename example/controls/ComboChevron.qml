import QtQuick
import Violet as S

Item {
    id: control

    property color strokeColor: S.Theme.muted

    implicitWidth: 12
    implicitHeight: 8

    Rectangle {
        x: 1
        y: 3
        width: 7
        height: 2
        radius: 1
        rotation: 45
        antialiasing: true
        color: control.strokeColor
    }

    Rectangle {
        x: 5
        y: 3
        width: 7
        height: 2
        radius: 1
        rotation: -45
        antialiasing: true
        color: control.strokeColor
    }
}
