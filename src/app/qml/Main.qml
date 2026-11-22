// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Picsel

ApplicationWindow {
    id: window

    required property Session session
    property int initialIndex: 0

    readonly property int current: mosaic.currentIndex
    readonly property bool fullViewOpen: fullViewLoader.active
    readonly property bool dialogOpen: confirmDialog.visible || conflictDialog.visible
                                       || progressDialog.visible || messageDialog.visible
                                       || clocksDialog.visible
                                       || window.session.moving

    width: 1600
    height: 1000
    visible: true
    visibility: Window.Maximized
    title: session.sourceDir.split("/").slice(-2).join("/") + " — picsel"
    color: Theme.window
    palette: Theme.palette

    function select(value) {
        if (mosaic.count === 0)
            return
        session.setSelected(mosaic.currentIndex, value)
        mosaic.advance()
    }

    function toggle() {
        if (mosaic.count > 0)
            session.toggleSelected(mosaic.currentIndex)
    }

    function undo() {
        const row = session.undo()
        if (row >= 0)
            mosaic.moveTo(row)
    }

    function openFullView() {
        if (mosaic.count > 0)
            fullViewLoader.active = true
    }

    function closeFullView() {
        fullViewLoader.active = false
        mosaic.forceActiveFocus()
    }

    // Selection keys work in both the mosaic and the full view.
    Shortcut { sequence: "S"; enabled: !window.dialogOpen; onActivated: window.select(true) }
    Shortcut { sequence: "D"; enabled: !window.dialogOpen; onActivated: window.select(false) }
    Shortcut { sequence: "T"; enabled: !window.dialogOpen; onActivated: window.toggle() }
    Shortcut { sequence: StandardKey.Undo; enabled: !window.dialogOpen; onActivated: window.undo() }

    function showMessage(title, text) {
        messageDialog.title = title
        messageDialog.text = text
        messageDialog.open()
    }

    // The final action: checks, confirmation, then the move itself.
    function requestMove() {
        if (session.moving)
            return
        const plan = session.prepareMove()
        if (plan.count === 0)
            return
        if (plan.fatal !== "") {
            showMessage(qsTr("Cannot move"), plan.fatal)
            return
        }
        if (plan.blocked.length > 0) {
            showMessage(qsTr("Cannot move"),
                        qsTr("These names are taken in %1 by something that isn't a regular file (a folder, link…). Nothing was moved.").arg(plan.target)
                        + "\n\n" + plan.blocked.join("\n"))
            return
        }
        confirmDialog.count = plan.count
        confirmDialog.total = plan.total
        confirmDialog.target = plan.target
        confirmDialog.conflicts = plan.conflicts.length
        confirmDialog.open()
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        SidePanel {
            id: sidePanel
            Layout.fillHeight: true
            Layout.preferredWidth: 360
            session: window.session
            currentIndex: mosaic.currentIndex
            onSelectRequested: window.select(true)
            onDeselectRequested: window.select(false)
            onMoveRequested: window.requestMove()
            onCameraClocksRequested: clocksDialog.open()
            onActivated: mosaic.forceActiveFocus()
        }

        Mosaic {
            id: mosaic
            Layout.fillWidth: true
            Layout.fillHeight: true
            session: window.session
            focus: true
            onOpenFullView: window.openFullView()
        }
    }

    Loader {
        id: fullViewLoader
        anchors.fill: parent
        active: false
        focus: active
        sourceComponent: FullView {
            session: window.session
            startIndex: mosaic.currentIndex
            onCloseRequested: window.closeFullView()
            onIndexChangedByUser: index => mosaic.moveTo(index)
        }
        onLoaded: item.forceActiveFocus()
    }

    // Selecting with S/D advances the mosaic; keep the full view in step.
    Connections {
        target: mosaic
        enabled: fullViewLoader.active
        function onCurrentIndexChanged() {
            if (fullViewLoader.item)
                fullViewLoader.item.showIndex(mosaic.currentIndex)
        }
    }

    Connections {
        target: window.session
        function onHighlightRequested(row) { mosaic.moveTo(row) }
        function onErrorOccurred(message) { window.showMessage(qsTr("picsel"), message) }
        function onMoveProgress(done, total, name) {
            progressDialog.done = done
            progressDialog.total = total
            progressDialog.current = name
        }
        function onMoveConflict(name) {
            conflictDialog.name = name
            conflictDialog.target = window.session.targetDir
            conflictDialog.open()
        }
        function onMovingChanged() {
            if (window.session.moving) {
                progressTimer.start()
            } else {
                progressTimer.stop()
                progressDialog.close()
                conflictDialog.close()
            }
        }
        function onMoveFinished(moved, skipped, failures, cancelled) {
            let text = (moved === 1 ? qsTr("Moved 1 picture to %1.")
                                    : qsTr("Moved %1 pictures to %2.").arg(moved)).arg(window.session.targetDir)
            if (skipped > 0)
                text += "\n" + qsTr("Skipped %1.").arg(skipped)
            if (cancelled)
                text += "\n" + qsTr("The move was stopped before the end.")
            if (failures.length > 0)
                text += "\n\n" + qsTr("Failed:") + "\n" + failures.join("\n")
            if (failures.length > 0 || skipped > 0 || cancelled)
                window.showMessage(qsTr("Move finished"), text)
            else
                toast.show(text)
        }
    }

    Timer {
        id: progressTimer
        interval: 300
        onTriggered: if (window.session.moving && !conflictDialog.visible) progressDialog.open()
    }

    ConfirmMoveDialog {
        id: confirmDialog
        onConfirmed: window.session.startMove()
        onClosed: mosaic.forceActiveFocus()
    }

    ConflictDialog {
        id: conflictDialog
        onAnswered: (action, applyToAll) => {
            if (action < 0)
                window.session.cancelMove()
            else
                window.session.resolveConflict(action, applyToAll)
            if (window.session.moving)
                progressTimer.restart()
        }
    }

    CameraClocksDialog {
        id: clocksDialog
        session: window.session
        onClosed: mosaic.forceActiveFocus()
    }

    MoveProgressDialog {
        id: progressDialog
        onCancelRequested: window.session.cancelMove()
    }

    Dialog {
        id: messageDialog
        property alias text: messageLabel.text
        anchors.centerIn: Overlay.overlay
        modal: true
        width: Math.min(720, window.width - 80)
        standardButtons: Dialog.Ok
        onClosed: mosaic.forceActiveFocus()
        onOpened: standardButton(Dialog.Ok).forceActiveFocus()
        ScrollView {
            anchors.fill: parent
            implicitHeight: Math.min(messageLabel.implicitHeight, window.height * 0.6)
            Label { id: messageLabel; width: messageDialog.availableWidth; wrapMode: Text.Wrap; color: Theme.text }
        }
    }

    // Brief confirmation that fades away on its own.
    Rectangle {
        id: toast
        function show(text) { toastLabel.text = text; toastAnim.restart() }
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 40
        width: toastLabel.implicitWidth + 48
        height: toastLabel.implicitHeight + 24
        radius: height / 2
        color: "#e0262a30"
        border.color: Theme.selected
        opacity: 0
        z: 100
        Label { id: toastLabel; anchors.centerIn: parent; color: Theme.text; font.pixelSize: Theme.fontLarge }
        SequentialAnimation {
            id: toastAnim
            NumberAnimation { target: toast; property: "opacity"; to: 1; duration: 150 }
            PauseAnimation { duration: 2500 }
            NumberAnimation { target: toast; property: "opacity"; to: 0; duration: 400 }
        }
    }

    Component.onCompleted: {
        mosaic.forceActiveFocus()
        Qt.callLater(() => mosaic.moveTo(initialIndex))
    }
}
