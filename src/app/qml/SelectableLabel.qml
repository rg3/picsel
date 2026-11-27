// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import Picsel

// Read-only text that can be selected with the mouse and copied with Ctrl+C
// (or pasted with a middle click where there is a primary selection). It
// never takes keyboard focus, so S/D/T and the arrow keys keep working.
// A long text is elided until hovered or selected; then it shows whole,
// clipped, and dragging past the edge scrolls it.
Item {
    id: root
    property string text
    property color color: Theme.text
    property alias font: input.font
    property int elide: Text.ElideMiddle
    readonly property bool whole: hover.hovered || input.selectedText !== "" || !shortened.truncated

    implicitWidth: shortened.implicitWidth
    implicitHeight: input.implicitHeight
    clip: true

    TextInput {
        id: input
        width: parent.width
        anchors.verticalCenter: parent.verticalCenter
        text: root.text
        color: root.color
        font.pixelSize: Theme.fontNormal
        readOnly: true
        selectByMouse: true
        activeFocusOnPress: false
        selectionColor: Theme.accent
        selectedTextColor: "white"
        opacity: root.whole ? 1 : 0
        // One selection at a time, so Ctrl+C knows what to copy.
        onSelectedTextChanged: {
            if (selectedText === "")
                return
            if (MouseSelection.owner && MouseSelection.owner !== input)
                MouseSelection.owner.deselect()
            MouseSelection.owner = input
        }
        // No onDestruction cleanup: QML nulls an Item property when its
        // object goes, and at shutdown the singleton may already be gone.
    }
    Label {
        id: shortened
        anchors.fill: parent
        verticalAlignment: Text.AlignVCenter
        text: root.text
        font: input.font
        color: root.color
        elide: root.elide
        opacity: root.whole ? 0 : 1
    }
    HoverHandler { id: hover; cursorShape: Qt.IBeamCursor }
    Shortcut {
        sequences: [StandardKey.Copy]
        enabled: input.selectedText !== ""
        onActivated: input.copy()
    }
}
