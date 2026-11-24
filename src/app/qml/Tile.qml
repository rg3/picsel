// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Effects

// One slot of the mosaic. The cell is 3:2; the picture is fitted and centred.
Item {
    id: tile

    required property int index
    required property string name
    required property int uid
    required property bool selected
    required property bool isVideo
    required property int thumbVersion
    required property bool thumbFailed
    required property int groupId
    required property bool groupStart
    required property bool groupEnd
    required property real durationMs

    property Session session
    property bool highlighted: false

    signal clicked()
    signal doubleClicked()

    z: highlighted ? 10 : 0

    Item {
        id: body
        anchors.fill: parent
        anchors.margins: Theme.gap / 2
        scale: tile.highlighted ? Theme.highlightScale : 1.0
        Behavior on scale { NumberAnimation { duration: Theme.animMs; easing.type: Easing.OutCubic } }

        // Drop shadow under the highlighted tile.
        RectangularShadow {
            anchors.fill: frame
            visible: tile.highlighted
            radius: 6
            blur: 24
            spread: 2
            color: "#c0000000"
        }

        // Outer ring marking the highlighted picture.
        Rectangle {
            anchors.fill: frame
            anchors.margins: -4
            radius: 8
            color: "transparent"
            border.width: 3
            border.color: Theme.highlightRing
            visible: tile.highlighted
        }

        Rectangle {
            id: frame
            anchors.fill: parent
            radius: 5
            color: Theme.tile
            border.width: Theme.frameWidth
            border.color: tile.selected ? Theme.selected : Theme.deselected
            Behavior on border.color { ColorAnimation { duration: Theme.animMs } }

            Image {
                id: thumb
                anchors.fill: parent
                anchors.margins: Theme.frameWidth
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: false
                retainWhileLoading: true
                smooth: true
                mipmap: false
                source: tile.thumbVersion > 0
                        ? tile.session.thumbUrl(tile.uid, tile.thumbVersion)
                        : ""
                opacity: status === Image.Ready ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 90 } }
            }

            // Placeholder until the thumbnail arrives.
            Column {
                anchors.centerIn: parent
                width: parent.width - 24
                spacing: 6
                visible: thumb.status !== Image.Ready
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: tile.thumbFailed ? "⚠" : (tile.isVideo ? "▶" : "")
                    color: Theme.textDim
                    font.pixelSize: 28
                    visible: text !== ""
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideMiddle
                    text: tile.name
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSmall
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("cannot decode")
                    color: Theme.deselected
                    font.pixelSize: Theme.fontSmall
                    visible: tile.thumbFailed
                }
            }

            SequentialAnimation on color {
                running: thumb.status !== Image.Ready && !tile.thumbFailed && !tile.isVideo
                loops: Animation.Infinite
                ColorAnimation { to: Qt.lighter(Theme.tile, 1.25); duration: 700 }
                ColorAnimation { to: Theme.tile; duration: 700 }
            }

            // Video badge.
            Rectangle {
                visible: tile.isVideo
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: Theme.frameWidth + 6
                radius: 4
                color: "#b0000000"
                width: videoLabel.implicitWidth + 12
                height: videoLabel.implicitHeight + 6
                Text {
                    id: videoLabel
                    anchors.centerIn: parent
                    color: "white"
                    font.pixelSize: Theme.fontSmall
                    text: "▶ " + (tile.durationMs >= 0 ? formatDuration(tile.durationMs) : "")
                    function formatDuration(ms) {
                        const s = Math.round(ms / 1000)
                        const m = Math.floor(s / 60)
                        return m + ":" + String(s % 60).padStart(2, "0")
                    }
                }
            }

            // Selection badge.
            Rectangle {
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: Theme.frameWidth + 6
                width: 26
                height: 26
                radius: 13
                color: tile.selected ? Theme.selected : Theme.deselected
                border.color: "#80000000"
                border.width: 1
                Text {
                    anchors.centerIn: parent
                    text: tile.selected ? "✓" : "✗"
                    color: "white"
                    font.pixelSize: 16
                    font.bold: true
                }
            }

            // Similar-pictures group bar.
            Rectangle {
                visible: tile.groupId >= 0
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: tile.groupStart ? Theme.frameWidth + 8 : -Theme.gap
                anchors.rightMargin: tile.groupEnd ? Theme.frameWidth + 8 : -Theme.gap
                anchors.bottomMargin: -Theme.gap / 2 - 2
                height: 4
                radius: 2
                // Evaluated even while hidden, so guard groupId == -1 (no group).
                color: tile.groupId >= 0 ? Theme.groupColors[tile.groupId % Theme.groupColors.length]
                                         : "transparent"
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onClicked: tile.clicked()
        onDoubleClicked: tile.doubleClicked()
    }
}
