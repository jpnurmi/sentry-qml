import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import "controls"

Item {
    id: header

    property bool canGoBack: false
    property bool showFeedbackButton: false

    signal backClicked
    signal feedbackClicked

    Layout.fillWidth: true
    implicitHeight: AppTheme.compact ? compactHeader.implicitHeight : desktopHeader.implicitHeight

    Item {
        id: desktopHeader

        readonly property real headerGap: 12
        readonly property real titleMaxWidth: parent.width
            - (headerActions.visible ? headerActions.implicitWidth + headerGap : 0)

        visible: !AppTheme.compact
        width: parent.width
        height: visible ? implicitHeight : 0
        implicitHeight: Math.max(titleRow.implicitHeight, headerActions.implicitHeight)

        RowLayout {
            id: titleRow

            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(0, Math.min(implicitWidth, desktopHeader.titleMaxWidth))
            spacing: AppTheme.formSpacing
            clip: true

            BackButton {
                visible: header.canGoBack
                onClicked: header.backClicked()
            }

            Image {
                Layout.preferredWidth: 42
                Layout.preferredHeight: 37
                Layout.leftMargin: 8
                Layout.rightMargin: 4
                source: "sentry-glyph.svg"
                sourceSize.width: width
                sourceSize.height: height
                fillMode: Image.PreserveAspectFit
                smooth: true
            }

            Label {
                text: qsTr("Sentry QML")
                color: AppTheme.text
                font.pixelSize: 34
                font.weight: Font.DemiBold
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
        }

        IconToolButton {
            id: headerActions

            visible: header.showFeedbackButton
            width: implicitWidth
            height: implicitHeight
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Feedback")
            Accessible.name: qsTr("Feedback")
            onClicked: header.feedbackClicked()
        }
    }

    RowLayout {
        id: compactHeader

        visible: AppTheme.compact
        width: parent.width
        height: visible ? implicitHeight : 0
        spacing: AppTheme.formSpacing

        BackButton {
            visible: header.canGoBack
            onClicked: header.backClicked()
        }

        Image {
            Layout.preferredWidth: 34
            Layout.preferredHeight: 30
            Layout.leftMargin: 4
            Layout.rightMargin: 2
            source: "sentry-glyph.svg"
            sourceSize.width: width
            sourceSize.height: height
            fillMode: Image.PreserveAspectFit
            smooth: true
        }

        Label {
            text: qsTr("Sentry QML")
            color: AppTheme.text
            font.pixelSize: 28
            font.weight: Font.DemiBold
            Layout.fillWidth: true
            elide: Text.ElideRight
        }

        IconToolButton {
            visible: header.showFeedbackButton
            text: qsTr("Feedback")
            Accessible.name: qsTr("Feedback")
            onClicked: header.feedbackClicked()
        }
    }
}
