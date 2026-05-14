pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T

T.ComboBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             Theme.controlHeight)
    leftPadding: 14
    rightPadding: 38
    topPadding: 6
    bottomPadding: 6

    delegate: ItemDelegate {
        required property int index
        required property var model

        width: ListView.view ? ListView.view.width : control.width
        text: model[control.textRole]
        highlighted: control.highlightedIndex === index
    }

    indicator: Text {
        x: control.mirrored ? control.leftPadding : control.width - width - 14
        y: (control.height - height) / 2
        width: 18
        height: 18
        text: "⌄"
        color: control.enabled ? Theme.muted : Theme.disabledText
        opacity: control.down ? 1.0 : control.hovered ? 0.85 : 0.65
        font.family: control.font.family
        font.pixelSize: Math.max(18, control.font.pixelSize)
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    contentItem: TextInput {
        leftPadding: control.mirrored ? control.indicator.width + control.spacing : 0
        rightPadding: control.mirrored ? 0 : control.indicator.width + control.spacing
        text: control.editable ? control.editText : control.displayText
        enabled: control.editable
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: control.inputMethodHints
        font: control.font
        color: control.enabled ? Theme.text : Theme.disabledText
        selectionColor: Theme.accent
        selectedTextColor: Theme.text
        verticalAlignment: TextInput.AlignVCenter
        autoScroll: control.editable
    }

    background: Rectangle {
        implicitWidth: 160
        implicitHeight: Theme.controlHeight
        radius: Theme.radius
        color: control.down || control.activeFocus ? Theme.inputFocus
            : control.hovered ? Theme.input
            : Theme.surfaceRaised
        border.width: control.visualFocus ? 2 : 1
        border.color: control.visualFocus ? Theme.accent : Theme.border
    }

    popup: T.Popup {
        y: control.height + 4
        width: control.width
        implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
        padding: 4

        contentItem: ListView {
            clip: true
            implicitHeight: Math.min(contentHeight, 280)
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
        }

        background: Rectangle {
            color: Theme.surface
            radius: Theme.radius
            border.width: 1
            border.color: Theme.border
        }
    }
}
