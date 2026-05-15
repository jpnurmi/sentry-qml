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
    property alias decimals: spinBox.decimals
    property alias locale: spinBox.locale

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

    DoubleSpinBox {
        id: spinBox

        editable: true
        hoverEnabled: true
        font.pixelSize: 15
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        Layout.fillWidth: true

        onValueModified: control.valueModified()
    }
}
