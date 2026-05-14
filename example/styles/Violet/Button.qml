import QtQuick
import QtQuick.Templates as T

T.Button {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             Theme.controlHeight)
    padding: 12
    leftPadding: 18
    rightPadding: 18
    spacing: 8

    icon.width: 18
    icon.height: 18
    icon.color: control.enabled ? Theme.text : Theme.disabledText

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.enabled ? Theme.text : Theme.disabledText
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        implicitWidth: 112
        implicitHeight: Theme.controlHeight
        radius: Theme.radius
        color: !control.enabled ? Theme.disabled
            : control.down ? Theme.inputFocus
            : control.hovered ? Theme.input
            : Theme.surfaceRaised
        border.width: control.visualFocus ? 2 : 1
        border.color: control.visualFocus ? Theme.accent : Theme.border
    }
}
