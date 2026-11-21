import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Picsel

// Almost-full-screen view of the highlighted picture over the mosaic.
// A horizontal ListView provides paging with a slide between pictures; each
// page shows the (instant) thumbnail scaled up until the full image arrives.
FocusScope {
    id: root

    required property Session session
    property int startIndex: 0
    readonly property int currentIndex: pages.currentIndex
    property bool videoMuted: true   // remembered while the full view is open

    signal closeRequested()
    signal indexChangedByUser(int index)

    function showIndex(i) {
        if (i !== pages.currentIndex && i >= 0 && i < pages.count)
            pages.currentIndex = i
    }

    function step(delta) {
        const i = pages.currentIndex + delta
        if (i >= 0 && i < pages.count) {
            pages.currentIndex = i
            root.indexChangedByUser(i)
        }
    }

    function reportBox() {
        const dpr = Screen.devicePixelRatio
        if (pages.width > 0 && pages.height > 0)
            session.setFullBox(Math.round(pages.width * dpr), Math.round(pages.height * dpr))
    }

    Keys.onPressed: event => {
        switch (event.key) {
        case Qt.Key_Left: step(-1); break
        case Qt.Key_Right: step(1); break
        case Qt.Key_Space:
            if (pages.currentItem && pages.currentItem.player)
                pages.currentItem.player.togglePlay()
            break
        case Qt.Key_M:
            if (pages.currentItem && pages.currentItem.isVideo)
                root.videoMuted = !root.videoMuted
            break
        case Qt.Key_Escape:
        case Qt.Key_Return:
        case Qt.Key_Enter: root.closeRequested(); break
        case Qt.Key_Up:
        case Qt.Key_Down:
        case Qt.Key_Home:
        case Qt.Key_End:
        case Qt.Key_PageUp:
        case Qt.Key_PageDown:
            break // swallow: no vertical navigation in the full view
        default:
            return
        }
        event.accepted = true
    }

    Rectangle {
        id: scrim
        anchors.fill: parent
        color: "#f20c0c0e"
        opacity: 0
        Component.onCompleted: opacity = 1
        Behavior on opacity { NumberAnimation { duration: 140 } }
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: wheel => wheel.accepted = true }
    }

    ListView {
        id: pages
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: bar.top
        anchors.margins: 16
        orientation: ListView.Horizontal
        snapMode: ListView.SnapOneItem
        highlightRangeMode: ListView.StrictlyEnforceRange
        highlightMoveDuration: 220
        highlightMoveVelocity: -1
        boundsBehavior: Flickable.StopAtBounds
        interactive: true
        cacheBuffer: width * 2
        clip: true
        model: root.session.model
        keyNavigationEnabled: false
        focus: true
        spacing: 40

        onCurrentIndexChanged: root.session.setFullFocus(currentIndex)
        onMovementEnded: root.indexChangedByUser(currentIndex)
        onWidthChanged: boxTimer.restart()
        onHeightChanged: boxTimer.restart()
        Component.onCompleted: {
            currentIndex = root.startIndex
            positionViewAtIndex(currentIndex, ListView.Beginning)
            root.reportBox()
            root.session.setFullFocus(currentIndex)
        }

        delegate: Item {
            id: page
            required property int index
            required property int uid
            required property int thumbVersion
            required property bool isVideo
            required property string name
            readonly property var player: videoLoader.item

            width: pages.width
            height: pages.height
            opacity: ListView.isCurrentItem ? 1 : 0.35
            Behavior on opacity { NumberAnimation { duration: 200 } }

            Image {
                id: thumb
                anchors.fill: parent
                anchors.bottomMargin: page.isVideo ? 52 : 0 // same area as the video
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: false
                smooth: true
                source: page.thumbVersion > 0
                        ? "image://thumb/%1/%2/%3".arg(root.session.sessionId).arg(page.uid).arg(page.thumbVersion)
                        : ""
                visible: (full.status !== Image.Ready || full.opacity < 1) && !(videoLoader.item && videoLoader.item.playing)
            }

            Image {
                id: full
                anchors.fill: parent
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: false
                smooth: true
                mipmap: true
                visible: !page.isVideo
                source: page.isVideo ? ""
                        : "image://full/%1/%2/%3".arg(root.session.sessionId).arg(page.uid).arg(page.thumbVersion)
                opacity: status === Image.Ready ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 120 } }
            }

            // Only the current page plays video; others show the thumbnail.
            Loader {
                id: videoLoader
                anchors.fill: parent
                active: page.isVideo && page.ListView.isCurrentItem && !pages.moving
                sourceComponent: VideoPlayer {
                    source: root.session.fileUrl(page.index)
                    muted: root.videoMuted
                    onMutedChanged: root.videoMuted = muted
                }
            }

            BusyIndicator {
                anchors.centerIn: parent
                running: !page.isVideo && full.status !== Image.Ready && page.ListView.isCurrentItem
                visible: running && thumb.status !== Image.Ready
            }
        }
    }

    Timer { id: boxTimer; interval: 150; onTriggered: root.reportBox() }

    // Bottom bar: file name, position, selection state and actions.
    Rectangle {
        id: bar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 76
        color: "#cc1c1c1f"

        readonly property var info: root.session.model.count > 0 ? itemInfo() : ({})
        function itemInfo() {
            // Re-evaluated on every count/selection change.
            root.session.selectedCount
            return root.session.info(pages.currentIndex)
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 24
            anchors.rightMargin: 24
            spacing: 16

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label {
                    Layout.fillWidth: true
                    text: bar.info.name ?? ""
                    color: Theme.text
                    font.pixelSize: Theme.fontLarge
                    font.bold: true
                    elide: Text.ElideMiddle
                }
                Label {
                    text: qsTr("%1 of %2").arg(pages.currentIndex + 1).arg(pages.count)
                          + ((bar.info.date ?? "") !== "" ? "  ·  " + bar.info.date : "")
                    color: Theme.textDim
                    font.pixelSize: Theme.fontNormal
                }
            }

            Rectangle {
                id: pill
                readonly property bool sel: bar.info.selected === true
                implicitWidth: pillText.implicitWidth + 32
                implicitHeight: 36
                radius: 18
                color: sel ? Theme.selected : Theme.deselected
                Behavior on color { ColorAnimation { duration: Theme.animMs } }
                Label {
                    id: pillText
                    anchors.centerIn: parent
                    text: pill.sel ? qsTr("✓  Selected") : qsTr("✗  Not selected")
                    color: "white"
                    font.pixelSize: Theme.fontLarge
                    font.bold: true
                }
            }

            Button {
                text: qsTr("Select (S)")
                focusPolicy: Qt.NoFocus
                onClicked: { root.session.setSelected(pages.currentIndex, true); root.step(1) }
            }
            Button {
                text: qsTr("Deselect (D)")
                focusPolicy: Qt.NoFocus
                onClicked: { root.session.setSelected(pages.currentIndex, false); root.step(1) }
            }
        }
    }

    // Close button.
    RoundButton {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 20
        width: 48
        height: 48
        text: "✕"
        font.pixelSize: 22
        focusPolicy: Qt.NoFocus
        onClicked: root.closeRequested()
    }

    // Edge arrows for the mouse.
    component EdgeButton: RoundButton {
        width: 56
        height: 56
        font.pixelSize: 24
        focusPolicy: Qt.NoFocus
        opacity: hovered ? 0.95 : 0.45
        anchors.verticalCenter: pages.verticalCenter
    }
    EdgeButton {
        anchors.left: parent.left
        anchors.leftMargin: 20
        text: "‹"
        visible: pages.currentIndex > 0
        onClicked: root.step(-1)
    }
    EdgeButton {
        anchors.right: parent.right
        anchors.rightMargin: 20
        text: "›"
        visible: pages.currentIndex < pages.count - 1
        onClicked: root.step(1)
    }

    Component.onDestruction: root.session.setFullFocus(-1)
}
