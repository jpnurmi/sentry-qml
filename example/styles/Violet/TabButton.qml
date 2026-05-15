import QtQuick
import QtQuick.Templates as T

T.TabButton {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             Theme.controlHeight)
    padding: 12
    leftPadding: 16
    rightPadding: 16

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.checked ? Theme.text : control.hovered ? Theme.muted : Theme.subtle
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        color: "transparent"

        Rectangle {
            width: parent.width
            height: control.checked ? 2 : control.hovered ? 1 : 0
            anchors.bottom: parent.bottom
            color: control.checked ? Theme.accent : Theme.border
        }
    }
}
