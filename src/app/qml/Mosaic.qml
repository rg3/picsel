import QtQuick
import QtQuick.Controls
import Picsel

// The grid of large 3:2 thumbnails with text-editor-like keyboard navigation.
FocusScope {
    id: root

    required property Session session
    property alias currentIndex: grid.currentIndex
    readonly property int count: grid.count
    readonly property int columns: session.columns

    signal openFullView()

    function moveTo(i) {
        if (count === 0)
            return
        grid.currentIndex = Math.max(0, Math.min(count - 1, i))
        ensureVisible(grid.currentIndex)
    }

    function advance() {
        if (grid.currentIndex < count - 1)
            moveTo(grid.currentIndex + 1)
    }

    // Scrolls just enough to show the row of |i| (with room for the zoom).
    function ensureVisible(i) {
        const row = Math.floor(i / columns)
        const y = row * grid.cellHeight
        const pad = grid.cellHeight * (Theme.highlightScale - 1) + Theme.gap
        let target = grid.contentY
        if (y - pad < grid.contentY)
            target = y - pad
        else if (y + grid.cellHeight + pad > grid.contentY + grid.height)
            target = y + grid.cellHeight + pad - grid.height
        const maxY = Math.max(-grid.topMargin, grid.contentHeight + grid.bottomMargin - grid.height)
        target = Math.max(-grid.topMargin, Math.min(maxY, target))
        if (target !== grid.contentY) {
            scrollAnim.stop()
            scrollAnim.from = grid.contentY
            scrollAnim.to = target
            scrollAnim.start()
        }
    }

    function reportVisible() {
        if (grid.cellHeight <= 0)
            return
        const firstRow = Math.max(0, Math.floor((grid.contentY) / grid.cellHeight))
        const lastRow = Math.floor((grid.contentY + grid.height) / grid.cellHeight)
        session.setVisibleRange(firstRow * columns, (lastRow + 1) * columns - 1)
    }

    function reportThumbSize() {
        const dpr = Screen.devicePixelRatio
        const inner = grid.cellWidth - Theme.gap - 2 * Theme.frameWidth
        if (inner > 0)
            session.setThumbBox(Math.round(inner * dpr), Math.round(inner * 2 / 3 * dpr))
    }

    function handleKey(event) {
        const i = grid.currentIndex
        const ctrl = event.modifiers & Qt.ControlModifier
        switch (event.key) {
        case Qt.Key_Left:
            moveTo(i - 1); break
        case Qt.Key_Right:
            moveTo(i + 1); break
        case Qt.Key_Up:
            if (i - columns >= 0) moveTo(i - columns)
            break
        case Qt.Key_Down:
            if (i + columns < count) moveTo(i + columns)
            else if (Math.floor(i / columns) < Math.floor((count - 1) / columns)) moveTo(count - 1)
            break
        case Qt.Key_Home:
            moveTo(0); break
        case Qt.Key_End:
            moveTo(count - 1); break
        case Qt.Key_PageUp:
            moveTo(i - columns * Math.max(1, Math.floor(grid.height / grid.cellHeight))); break
        case Qt.Key_PageDown:
            moveTo(i + columns * Math.max(1, Math.floor(grid.height / grid.cellHeight))); break
        case Qt.Key_Return:
        case Qt.Key_Enter:
            if (count > 0) root.openFullView()
            break
        case Qt.Key_Plus:
        case Qt.Key_Equal:
            session.columns = columns - 1; break
        case Qt.Key_Minus:
            session.columns = columns + 1; break
        default:
            return
        }
        event.accepted = true
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.window
    }

    GridView {
        id: grid
        anchors.fill: parent
        focus: true
        clip: true
        model: root.session.model
        topMargin: Theme.gap * 2
        bottomMargin: Theme.gap * 2
        leftMargin: Theme.gap * 2
        rightMargin: Theme.gap * 2
        cellWidth: Math.floor((width - leftMargin - rightMargin) / root.columns)
        cellHeight: Math.round(cellWidth * 2 / 3)
        cacheBuffer: cellHeight * 2
        highlightFollowsCurrentItem: false
        keyNavigationEnabled: false
        boundsBehavior: Flickable.StopAtBounds
        reuseItems: true
        currentIndex: 0

        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        delegate: Tile {
            width: grid.cellWidth
            height: grid.cellHeight
            sessionId: root.session.sessionId
            highlighted: index === grid.currentIndex
            onClicked: { grid.currentIndex = index; root.forceActiveFocus() }
            onDoubleClicked: { grid.currentIndex = index; root.openFullView() }
        }

        Keys.onPressed: event => root.handleKey(event)
        onCurrentIndexChanged: root.session.setCurrentRow(currentIndex)

        onContentYChanged: visibleTimer.restart()
        onHeightChanged: visibleTimer.restart()
        onCellWidthChanged: { sizeTimer.restart(); visibleTimer.restart() }
        onCountChanged: visibleTimer.restart()

        NumberAnimation {
            id: scrollAnim
            target: grid
            property: "contentY"
            duration: Theme.animMs + 40
            easing.type: Easing.OutCubic
        }
    }

    Timer { id: visibleTimer; interval: 16; onTriggered: root.reportVisible() }
    Timer { id: sizeTimer; interval: 150; onTriggered: root.reportThumbSize() }

    onColumnsChanged: Qt.callLater(() => ensureVisible(grid.currentIndex))

    Component.onCompleted: {
        session.setCurrentRow(grid.currentIndex)
        reportThumbSize()
        reportVisible()
    }
}
