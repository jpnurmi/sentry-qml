import Violet as S

Icon {
    id: control

    property bool active: false

    source: "qrc:/images/chevron-down.svg"
    opacity: active ? 1 : 0.65
    implicitWidth: 12
    implicitHeight: 8
}
