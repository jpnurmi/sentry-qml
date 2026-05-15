import QtQuick

Item {
    id: control

    property alias source: image.source

    implicitWidth: 24
    implicitHeight: 24
    width: implicitWidth
    height: implicitHeight

    Image {
        id: image

        anchors.fill: parent
        fillMode: Image.PreserveAspectFit
        sourceSize.width: Math.max(1, width)
        sourceSize.height: Math.max(1, height)
    }
}
