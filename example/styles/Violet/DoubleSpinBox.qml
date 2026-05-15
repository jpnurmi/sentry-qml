import QtQuick
import QtQuick.Templates as T

T.DoubleSpinBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentItem.implicitWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentItem.implicitHeight + topPadding + bottomPadding,
                             Theme.controlHeight)
    leftPadding: 16
    rightPadding: 36
    topPadding: 8
    bottomPadding: 8

    validator: DoubleValidator {
        locale: control.locale.name
        bottom: Math.min(control.from, control.to)
        top: Math.max(control.from, control.to)
        decimals: control.decimals
        notation: DoubleValidator.StandardNotation
    }

    contentItem: TextInput {
        z: 2
        text: control.displayText
        font: control.font
        color: control.enabled ? Theme.text : Theme.disabledText
        selectionColor: Theme.accent
        selectedTextColor: Theme.text
        horizontalAlignment: Qt.AlignLeft
        verticalAlignment: Qt.AlignVCenter
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: Qt.ImhFormattedNumbersOnly
    }

    up.indicator: Item {
        x: control.mirrored ? 0 : control.width - width
        y: 0
        width: 32
        height: control.height / 2
        property bool available: control.enabled && control.value < control.to

        HoverHandler {
            id: upHover
            enabled: parent.available
        }

        Text {
            x: 0
            y: 3
            width: parent.width
            height: parent.height
            text: "+"
            font.family: control.font.family
            font.pixelSize: control.font.pixelSize
            font.bold: true
            color: Theme.muted
            opacity: !parent.available ? 0.25 : control.up.pressed ? 1.0 : upHover.hovered ? 0.8 : 0.55
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    down.indicator: Item {
        x: control.mirrored ? 0 : control.width - width
        y: control.height / 2
        width: 32
        height: control.height / 2
        property bool available: control.enabled && control.value > control.from

        HoverHandler {
            id: downHover
            enabled: parent.available
        }

        Text {
            x: 0
            y: -3
            width: parent.width
            height: parent.height
            text: "-"
            font.family: control.font.family
            font.pixelSize: control.font.pixelSize
            font.bold: true
            color: Theme.muted
            opacity: !parent.available ? 0.25 : control.down.pressed ? 1.0 : downHover.hovered ? 0.8 : 0.55
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    background: Rectangle {
        implicitWidth: 140
        implicitHeight: Theme.controlHeight
        radius: Theme.radius
        color: control.activeFocus ? Theme.inputFocus : Theme.input
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? Theme.accent : Theme.border
    }
}
