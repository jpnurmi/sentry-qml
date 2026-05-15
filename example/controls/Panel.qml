import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: control

    default property alias content: contentItem.data
    property alias actions: actions.data
    property string title: ""
    property int spacing: AppTheme.panelSpacing

    implicitHeight: layout.implicitHeight + AppTheme.panelMargin
    color: AppTheme.surface
    radius: 8

    ColumnLayout {
        id: layout

        anchors.fill: parent
        anchors.margins: AppTheme.panelMargin
        anchors.topMargin: 0
        spacing: control.spacing

        RowLayout {
            visible: control.title.length > 0 || actions.children.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            spacing: AppTheme.labelSpacing

            Label {
                text: control.title
                visible: control.title.length > 0
                color: AppTheme.text
                font.pixelSize: 16
                font.weight: Font.DemiBold
                font.capitalization: Font.AllUppercase
                verticalAlignment: Text.AlignVCenter
                Layout.fillWidth: true
                Layout.preferredHeight: 32
            }

            RowLayout {
                id: actions

                spacing: AppTheme.labelSpacing
            }
        }

        Item {
            id: contentItem

            readonly property real implicitContentHeight: children.length === 1
                ? children[0].implicitHeight : childrenRect.height

            implicitHeight: implicitContentHeight
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}
