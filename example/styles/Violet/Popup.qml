import QtQuick
import QtQuick.Templates as T

T.Popup {
    id: control

    padding: 18

    background: Rectangle {
        color: Theme.surface
        radius: Theme.radius
        border.width: 1
        border.color: Theme.border
    }
}
