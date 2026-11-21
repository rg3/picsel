import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Picsel

// Shown only when a move takes longer than a moment (e.g. copying across
// filesystems); same-filesystem moves finish before it appears.
Dialog {
    id: root

    property int done: 0
    property int total: 0
    property string current: ""

    signal cancelRequested()

    anchors.centerIn: Overlay.overlay
    modal: true
    closePolicy: Popup.NoAutoClose
    title: qsTr("Moving pictures…")
    width: Math.min(560, parent.width - 80)

    ColumnLayout {
        width: parent.width
        spacing: 10
        ProgressBar {
            Layout.fillWidth: true
            from: 0
            to: Math.max(1, root.total)
            value: root.done
        }
        Label {
            Layout.fillWidth: true
            elide: Text.ElideMiddle
            color: Theme.textDim
            text: qsTr("%1 of %2 · %3").arg(root.done).arg(root.total).arg(root.current)
        }
    }

    footer: DialogButtonBox {
        Button {
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: root.cancelRequested()
        }
    }
}
