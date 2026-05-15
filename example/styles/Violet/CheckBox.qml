import QtQuick
import QtQuick.Templates as T

T.CheckBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)
    spacing: 12
    padding: 0

    indicator: Rectangle {
        x: control.text.length > 0
            ? control.leftPadding
            : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        implicitWidth: 20
        implicitHeight: 20
        radius: 5
        color: {
            if (!control.enabled)
                return Theme.disabled;
            if (control.checked)
                return Theme.accent;
            if (control.hovered)
                return Theme.inputFocus;
            return Theme.input;
        }
        border.width: control.visualFocus ? 2 : 1
        border.color: control.visualFocus ? Theme.accent : Theme.border

        Rectangle {
            width: 8
            height: 8
            radius: 2
            anchors.centerIn: parent
            visible: control.checked
            color: Theme.text
        }
    }

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.enabled ? Theme.text : Theme.disabledText
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        leftPadding: control.indicator && !control.mirrored ? control.indicator.width + control.spacing : 0
        rightPadding: control.indicator && control.mirrored ? control.indicator.width + control.spacing : 0
    }
}
