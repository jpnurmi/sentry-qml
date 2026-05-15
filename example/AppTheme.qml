pragma Singleton

import QtCore
import QtQuick

QtObject {
    id: theme

    readonly property color background: "#101014"
    readonly property color surface: "#1b1b20"
    readonly property color surfaceRaised: "#27272d"
    readonly property color input: "#2d2d33"
    readonly property color inputFocus: "#343440"
    readonly property color border: "#4a4a55"
    readonly property color text: "#f7f4ff"
    readonly property color muted: "#b8b2ca"
    readonly property color subtle: "#8d879d"
    readonly property color accent: "#7553ff"
    readonly property color accentHover: "#8468ff"
    readonly property color accentPressed: "#6442e8"
    readonly property color success: "#2ecc71"
    readonly property color warning: "#f2b84b"
    readonly property color info: "#70b8ff"
    readonly property color critical: "#e1567c"
    readonly property color criticalHover: "#ee6b91"
    readonly property color disabled: "#2b2c32"
    readonly property color disabledText: "#777183"

    property bool compact: false

    readonly property int compactWidth: 900
    readonly property int pageMargin: compact ? 16 : 20
    readonly property int panelMargin: 16
    readonly property int controlHeight: 40
    readonly property int pageSpacing: 16
    readonly property int groupSpacing: 16
    readonly property int panelSpacing: 12
    readonly property int formSpacing: 12
    readonly property int labelSpacing: 8
}
