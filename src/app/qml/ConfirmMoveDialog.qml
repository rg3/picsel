// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Picsel

// "Move X of Y pictures to D?" — "No" is the default (focused) answer.
Dialog {
    id: root

    property int count: 0
    property int total: 0
    property string target: ""
    property int conflicts: 0

    signal confirmed()

    anchors.centerIn: Overlay.overlay
    modal: true
    closePolicy: Popup.CloseOnEscape
    width: Math.min(720, parent.width - 80)
    padding: 24
    topPadding: 8

    onOpened: noButton.forceActiveFocus(Qt.TabFocusReason)

    header: Label {
        text: qsTr("Move pictures")
        padding: 24
        bottomPadding: 4
        color: Theme.text
        font.pixelSize: Theme.fontLarge + 2
        font.bold: true
    }

    ColumnLayout {
        width: parent.width
        spacing: 12
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontLarge
            color: Theme.text
            text: root.count === 1 ? qsTr("Move 1 of %1 pictures to this folder?").arg(root.total)
                  : qsTr("Move %1 of %2 pictures to this folder?").arg(root.count).arg(root.total)
        }
        PathBox { text: root.target }
        Label {
            Layout.fillWidth: true
            visible: root.conflicts > 0
            wrapMode: Text.Wrap
            color: Theme.textDim
            text: root.conflicts === 1 ? qsTr("1 of them already exists there; you will be asked what to do.")
                  : qsTr("%1 of them already exist there; you will be asked what to do.").arg(root.conflicts)
        }
    }

    footer: Item {
        implicitHeight: buttons.implicitHeight + 24 + 8
        RowLayout {
            id: buttons
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.rightMargin: 24
            anchors.bottomMargin: 24
            spacing: 12
            DialogButton {
                text: qsTr("Yes, move")
                glyph: "✓"
                glyphColor: Theme.selected
                onClicked: { root.close(); root.confirmed() }
            }
            DialogButton {
                id: noButton
                text: qsTr("No")
                glyph: "✗"
                glyphColor: Theme.deselected
                onClicked: root.close()
            }
        }
    }
}
