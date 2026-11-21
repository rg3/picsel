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
    title: qsTr("File already exists")
    width: Math.min(680, parent.width - 80)

    onOpened: { applyAll.checked = false; keepBoth.forceActiveFocus() }

    function answer(action) {
        root.answered(action, applyAll.checked)
        root.close()
    }

    ColumnLayout {
        width: parent.width
        spacing: 14
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.text
            font.pixelSize: Theme.fontLarge
            text: qsTr("“%1” already exists in %2.").arg(root.name).arg(root.target)
        }
        CheckBox {
            id: applyAll
            text: qsTr("Do the same for the remaining conflicts")
        }
    }

    footer: DialogButtonBox {
        component ActionButton: Button {
            Keys.onReturnPressed: clicked()
            Keys.onEnterPressed: clicked()
        }
        ActionButton { text: qsTr("Overwrite"); onClicked: root.answer(0) }
        ActionButton { id: keepBoth; text: qsTr("Keep both (add suffix)"); onClicked: root.answer(1) }
        ActionButton { text: qsTr("Skip"); onClicked: root.answer(2) }
        ActionButton { text: qsTr("Stop moving"); onClicked: { root.answered(-1, false); root.close() } }
    }
}
