import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import Picsel

// Video playback for the full view: starts paused and muted.
Item {
    id: root

    property url source
    property bool muted: true

    readonly property bool playing: player.playbackState === MediaPlayer.PlayingState

    function togglePlay() {
        if (playing)
            player.pause()
        else
            player.play()
    }

    function format(ms) {
        const s = Math.floor(ms / 1000)
        return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0")
    }

    MediaPlayer {
        id: player
        source: root.source
        videoOutput: output
        audioOutput: AudioOutput { muted: root.muted }
    }

    VideoOutput {
        id: output
        anchors.fill: parent
        anchors.bottomMargin: controls.height + 8
        fillMode: VideoOutput.PreserveAspectFit
    }

    MouseArea {
        anchors.fill: output
        onClicked: root.togglePlay()
    }

    // Big play hint while paused.
    Rectangle {
        anchors.centerIn: output
        width: 96
        height: 96
        radius: 48
        color: "#90000000"
        visible: !root.playing
        Label { anchors.centerIn: parent; text: "▶"; color: "white"; font.pixelSize: 44 }
    }

    RowLayout {
        id: controls
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 44
        spacing: 12

        Button {
            text: root.playing ? qsTr("❚❚ Pause") : qsTr("▶ Play")
            focusPolicy: Qt.NoFocus
            onClicked: root.togglePlay()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Space")
        }
        Label { text: root.format(player.position); color: Theme.text }
        Slider {
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            from: 0
            to: Math.max(1, player.duration)
            value: player.position
            enabled: player.seekable
            onMoved: player.position = value
        }
        Label { text: root.format(player.duration); color: Theme.text }
        Button {
            text: root.muted ? qsTr("🔇 Muted") : qsTr("🔊 Sound")
            focusPolicy: Qt.NoFocus
            onClicked: root.muted = !root.muted
            ToolTip.visible: hovered
            ToolTip.text: qsTr("M")
        }
    }
}
