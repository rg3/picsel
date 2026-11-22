// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Picsel

// A selected picture's name already exists in the destination.
Dialog {
    id: root

    property string name: ""
    property string target: ""

    // action: 0 overwrite, 1 keep both, 2 skip; -1 cancel the whole move
    signal answered(int action, bool applyToAll)

    anchors.centerIn: Overlay.overlay
    modal: true
    closePolicy: Popup.NoAutoClose
    width: Math.min(760, parent.width - 80)
    padding: 24
    topPadding: 8

    onOpened: { applyAll.checked = false; keepBoth.forceActiveFocus(Qt.TabFocusReason) }

    function answer(action) {
        root.answered(action, applyAll.checked)
        root.close()
    }

    header: Label {
        text: qsTr("File already exists")
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
            color: Theme.text
            font.pixelSize: Theme.fontLarge
            text: qsTr("“%1” already exists in").arg(root.name)
        }
        PathBox { text: root.target }
        CheckBox {
            id: applyAll
            focusPolicy: Qt.NoFocus
            text: qsTr("Do the same for the remaining conflicts")
        }
    }

    footer: Item {
        implicitHeight: buttons.implicitHeight + 32
        RowLayout {
            id: buttons
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.rightMargin: 24
            anchors.bottomMargin: 24
            spacing: 12
            DialogButton { text: qsTr("Overwrite"); glyph: "⟳"; glyphColor: Theme.deselected; onClicked: root.answer(0) }
            DialogButton { id: keepBoth; text: qsTr("Keep both (add suffix)"); glyph: "＋"; glyphColor: Theme.selected; onClicked: root.answer(1) }
            DialogButton { text: qsTr("Skip"); glyph: "→"; glyphColor: Theme.textDim; onClicked: root.answer(2) }
            DialogButton { text: qsTr("Stop moving"); glyph: "■"; glyphColor: Theme.deselected; onClicked: { root.answered(-1, false); root.close() } }
        }
    }
}
