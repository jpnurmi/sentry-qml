import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Violet as S

ColumnLayout {
    id: control

    property string label: ""
    property alias value: spinBox.value
    property alias from: spinBox.from
    property alias to: spinBox.to
    property alias stepSize: spinBox.stepSize

    signal valueModified()

    spacing: AppTheme.labelSpacing
    Layout.fillWidth: true

    Label {
        text: control.label
        color: S.Theme.muted
        font.pixelSize: 12
        font.weight: Font.DemiBold
        Layout.fillWidth: true
    }

    SpinBox {
        id: spinBox

        editable: true
        hoverEnabled: true
        font.pixelSize: 15
        Layout.fillWidth: true
        textFromValue: function(value) {
            return String(value);
        }
        valueFromText: function(text) {
            const value = Number(text);
            if (!isFinite(value))
                return spinBox.value;
            return Math.round(value);
        }

        onValueModified: control.valueModified()
    }
}
