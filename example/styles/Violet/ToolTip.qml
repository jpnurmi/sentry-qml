import QtQuick
import QtQuick.Templates as T

T.ToolTip {
    id: control

    x: parent ? (parent.width - implicitWidth) / 2 : 0
    y: parent ? -implicitHeight - 8 : 0
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)
    margins: 8
    padding: 8
    leftPadding: 10
    rightPadding: 10
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent | T.Popup.CloseOnReleaseOutsideParent

    contentItem: Text {
        text: control.text
        font.family: control.font.family
        font.pixelSize: 12
        font.weight: Font.DemiBold
        color: Theme.text
        wrapMode: Text.NoWrap
    }

    background: Rectangle {
        color: Theme.surfaceRaised
        radius: Theme.radius
        border.width: 1
        border.color: Theme.border
    }
}
