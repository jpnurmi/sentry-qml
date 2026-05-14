import QtQuick
import QtQuick.Controls
import Violet as S

ToolButton {
    id: control

    property bool destructive: false
    property bool quietDestructive: false
    property string tooltip: ""
    readonly property bool iconOnly: text.length <= 1

    hoverEnabled: true
    implicitWidth: iconOnly ? 28 : Math.max(36, label.implicitWidth + 24)
    implicitHeight: iconOnly ? 28 : 36
    padding: iconOnly ? 0 : 8

    contentItem: Text {
        id: label

        text: control.text
        color: {
            if (!control.enabled)
                return S.Theme.disabledText;
            if (control.quietDestructive)
                return control.hovered || control.down ? S.Theme.critical : S.Theme.muted;
            return S.Theme.text;
        }
        font.pixelSize: control.text.length > 1 ? 14 : 15
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: control.iconOnly ? width / 2 : S.Theme.radius
        color: {
            if (!control.enabled)
                return S.Theme.disabled;
            if (control.quietDestructive)
                return control.down ? S.Theme.surfaceRaised : control.hovered ? "#24242b" : "transparent";
            if (control.destructive)
                return control.down ? Qt.darker(S.Theme.critical, 1.12) : control.hovered ? S.Theme.criticalHover : "transparent";
            return control.down ? S.Theme.surfaceRaised : control.hovered ? "#24242b" : "transparent";
        }
    }

    ToolTip {
        text: control.tooltip
        visible: control.tooltip.length > 0 && control.hovered
        x: Math.round((control.width - width) / 2)
        y: -height - 8
    }
}
