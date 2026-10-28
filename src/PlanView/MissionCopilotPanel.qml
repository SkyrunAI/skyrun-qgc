import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// Takes a typed instruction and replaces the plan with the mission it describes.
Rectangle {
    id: root
    width: Math.min(ScreenTools.defaultFontPixelWidth * 60, parent.width - x - _margin)
    height: layout.height + (_margin * 2)
    color: qgcPal.windowPanel
    radius: ScreenTools.defaultBorderRadius

    required property var missionController

    readonly property real _margin: ScreenTools.defaultFontPixelWidth
    readonly property var _copilot: missionController.copilot

    QGCPalette { id: qgcPal }

    Connections {
        target: _copilot
        function onSaid(text) { status.text = text }
    }

    ColumnLayout {
        id: layout
        anchors.margins: _margin
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: _margin

        RowLayout {
            Layout.fillWidth: true
            spacing: _margin

            QGCTextField {
                id: instruction
                Layout.fillWidth: true
                enabled: !_copilot.busy
                placeholderText: qsTr("Take off to 40 m, fly a 200 m square around home, come back and land.")
                onAccepted: root._ask()
            }

            QGCButton {
                text: _copilot.busy ? qsTr("Working") : qsTr("Build")
                enabled: !_copilot.busy && instruction.text.trim() !== ""
                onClicked: root._ask()
            }
        }

        QGCLabel {
            id: status
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            visible: text !== ""
        }
    }

    function _ask() {
        status.text = qsTr("Planning…")
        _copilot.ask(instruction.text)
    }
}
