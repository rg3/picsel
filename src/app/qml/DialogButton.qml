// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import Picsel

// Dialog button with a small coloured icon and an unmistakable focus ring,
// so the default answer is obvious. Return/Enter activate it; Left/Right
// move between buttons.
Button {
    id: root

    property string glyph: ""
    property color glyphColor: Theme.text

    focusPolicy: Qt.StrongFocus
    leftPadding: 18
    rightPadding: 18
    topPadding: 9
    bottomPadding: 9
    font.pixelSize: Theme.fontNormal

    Keys.onReturnPressed: clicked()
    Keys.onEnterPressed: clicked()
    Keys.onLeftPressed: nextItemInFocusChain(false).forceActiveFocus(Qt.BacktabFocusReason)
    Keys.onRightPressed: nextItemInFocusChain(true).forceActiveFocus(Qt.TabFocusReason)

    contentItem: Row {
        spacing: 8
        Label {
            visible: root.glyph !== ""
            text: root.glyph
            color: root.glyphColor
            font.pixelSize: root.font.pixelSize + 2
            font.bold: true
            anchors.verticalCenter: parent.verticalCenter
        }
        Label {
            text: root.text
            color: Theme.text
            font: root.font
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    background: Rectangle {
        implicitWidth: 110
        radius: 6
        color: root.down ? Qt.darker(Theme.panelRaised, 1.3)
             : root.hovered ? Qt.lighter(Theme.panelRaised, 1.25) : Theme.panelRaised
        border.width: root.activeFocus ? 2 : 1
        border.color: root.activeFocus ? Theme.accent : Theme.border
        // Outer glow ring for the focused (default) button.
        Rectangle {
            anchors.fill: parent
            anchors.margins: -4
            radius: 9
            color: "transparent"
            border.width: 2
            border.color: Qt.alpha(Theme.accent, 0.45)
            visible: root.activeFocus
        }
    }
}
