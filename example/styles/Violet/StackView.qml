import QtQuick
import QtQuick.Templates as T

T.StackView {
    id: control

    readonly property real transitionDistance: 0.4 * width
    readonly property int moveDuration: 320
    readonly property int fadeDuration: 240
    readonly property int direction: mirrored ? -1 : 1

    popEnter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "x"; from: -control.direction * control.transitionDistance; to: 0; duration: control.moveDuration; easing.type: Easing.OutCubic }
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: control.fadeDuration; easing.type: Easing.InQuint }
        }
    }

    popExit: Transition {
        ParallelAnimation {
            NumberAnimation { property: "x"; from: 0; to: control.direction * control.transitionDistance; duration: control.moveDuration; easing.type: Easing.OutCubic }
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: control.fadeDuration; easing.type: Easing.OutQuint }
        }
    }

    pushEnter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "x"; from: control.direction * control.transitionDistance; to: 0; duration: control.moveDuration; easing.type: Easing.OutCubic }
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: control.fadeDuration; easing.type: Easing.InQuint }
        }
    }

    pushExit: Transition {
        ParallelAnimation {
            NumberAnimation { property: "x"; from: 0; to: -control.direction * control.transitionDistance; duration: control.moveDuration; easing.type: Easing.OutCubic }
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: control.fadeDuration; easing.type: Easing.OutQuint }
        }
    }

    replaceEnter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "x"; from: control.direction * control.transitionDistance; to: 0; duration: control.moveDuration; easing.type: Easing.OutCubic }
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: control.fadeDuration; easing.type: Easing.InQuint }
        }
    }

    replaceExit: Transition {
        ParallelAnimation {
            NumberAnimation { property: "x"; from: 0; to: -control.direction * control.transitionDistance; duration: control.moveDuration; easing.type: Easing.OutCubic }
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: control.fadeDuration; easing.type: Easing.OutQuint }
        }
    }
}
