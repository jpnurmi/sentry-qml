import QtQuick
import QtQuick.Controls
import Violet as S

Button {
    id: control

    property bool primary: false
    property bool destructive: false
    property int preferredWidth: 152

    hoverEnabled: true
    font.pixelSize: 14
    font.weight: Font.DemiBold
    implicitWidth: preferredWidth
    implicitHeight: S.Theme.controlHeight
    leftPadding: 14
    rightPadding: 14

    contentItem: Text {
        text: control.text
        color: control.enabled ? S.Theme.text : S.Theme.disabledText
        font: control.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: S.Theme.radius
        color: {
            if (!control.enabled)
                return S.Theme.disabled;
            if (control.destructive)
                return control.down ? Qt.darker(S.Theme.critical, 1.12) : control.hovered ? S.Theme.criticalHover : S.Theme.critical;
            if (control.primary)
                return control.down ? S.Theme.accentPressed : control.hovered ? S.Theme.accentHover : S.Theme.accent;
            return control.down ? Qt.darker(S.Theme.surfaceRaised, 1.15) : control.hovered ? "#33333d" : S.Theme.surfaceRaised;
        }
        border.color: control.primary || control.destructive || !control.enabled ? "transparent" : S.Theme.border
        border.width: 1
    }
}
