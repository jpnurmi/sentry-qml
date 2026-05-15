import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Violet as S

Rectangle {
    id: control

    property int severity: 0
    property alias text: label.text

    implicitWidth: layout.implicitWidth + 28
    implicitHeight: 36
    radius: height / 2
    color: severity === 2 ? "#4a1d2d" : severity === 1 ? "#16372d" : S.Theme.surface

    RowLayout {
        id: layout

        anchors.centerIn: parent
        spacing: 8

        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: 8
            implicitHeight: 8
            radius: 4
            color: control.severity === 2 ? S.Theme.critical : control.severity === 1 ? S.Theme.success : S.Theme.subtle
        }

        Label {
            id: label

            color: control.severity === 2 ? S.Theme.critical : control.severity === 1 ? S.Theme.success : S.Theme.muted
            font.pixelSize: 15
            font.weight: Font.DemiBold
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }
    }
}
