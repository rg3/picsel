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
    Shortcut { sequence: "S"; onActivated: window.select(true) }
    Shortcut { sequence: "D"; onActivated: window.select(false) }
    Shortcut { sequence: "T"; onActivated: window.toggle() }
    Shortcut { sequence: StandardKey.Undo; onActivated: window.undo() }

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
        source: "FullView.qml"
        onLoaded: item.forceActiveFocus()
    }

    Connections {
        target: window.session
        function onHighlightRequested(row) { mosaic.moveTo(row) }
        function onErrorOccurred(message) { errorDialog.text = message; errorDialog.open() }
    }

    Dialog {
        id: errorDialog
        property alias text: errorLabel.text
        anchors.centerIn: parent
        modal: true
        title: qsTr("picsel")
        standardButtons: Dialog.Ok
        Label { id: errorLabel; wrapMode: Text.Wrap; width: 480 }
    }

    Component.onCompleted: {
        mosaic.forceActiveFocus()
        Qt.callLater(() => mosaic.moveTo(initialIndex))
    }
}
