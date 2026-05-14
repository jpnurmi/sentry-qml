import QtQuick
import QtQuick.Shapes
import Violet as S

Shape {
    id: control

    property color strokeColor: S.Theme.text

    implicitWidth: 22
    implicitHeight: 16

    ShapePath {
        strokeColor: control.strokeColor
        strokeWidth: 2
        fillColor: "transparent"
        capStyle: ShapePath.RoundCap
        joinStyle: ShapePath.RoundJoin
        startX: 1
        startY: 2

        PathLine { x: 21; y: 2 }
        PathLine { x: 21; y: 14 }
        PathLine { x: 1; y: 14 }
        PathLine { x: 1; y: 2 }
    }

    ShapePath {
        strokeColor: control.strokeColor
        strokeWidth: 2
        fillColor: "transparent"
        capStyle: ShapePath.RoundCap
        joinStyle: ShapePath.RoundJoin
        startX: 2
        startY: 3

        PathLine { x: 11; y: 9 }
        PathLine { x: 20; y: 3 }
    }

    ShapePath {
        strokeColor: control.strokeColor
        strokeWidth: 2
        fillColor: "transparent"
        capStyle: ShapePath.RoundCap
        startX: 2
        startY: 14

        PathLine { x: 8; y: 9 }
    }

    ShapePath {
        strokeColor: control.strokeColor
        strokeWidth: 2
        fillColor: "transparent"
        capStyle: ShapePath.RoundCap
        startX: 20
        startY: 14

        PathLine { x: 14; y: 9 }
    }
}
