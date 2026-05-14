import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Violet as S

ColumnLayout {
    id: control

    property string label: ""
    property alias text: textField.text
    property alias placeholderText: textField.placeholderText
    property alias inputMethodHints: textField.inputMethodHints
    property alias readOnly: textField.readOnly
    property alias validator: textField.validator

    signal textEdited()
    signal editingFinished()

    spacing: 6
    Layout.fillWidth: true

    Label {
        text: control.label
        color: S.Theme.muted
        font.pixelSize: 12
        font.weight: Font.DemiBold
        Layout.fillWidth: true
    }

    TextField {
        id: textField

        font.pixelSize: 15
        Layout.fillWidth: true

        onTextEdited: control.textEdited()
        onEditingFinished: control.editingFinished()
    }
}
