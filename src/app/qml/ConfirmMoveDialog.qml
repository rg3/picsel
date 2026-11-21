import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Picsel

// "Move X of Y pictures to D?" — "No" is the default button.
Dialog {
    id: root

    property int count: 0
    property int total: 0
    property string target: ""
    property int conflicts: 0

    signal confirmed()

    anchors.centerIn: Overlay.overlay
    modal: true
    closePolicy: Popup.CloseOnEscape
    title: qsTr("Move pictures")
    width: Math.min(640, parent.width - 80)

    onOpened: noButton.forceActiveFocus()

    ColumnLayout {
        width: parent.width
        spacing: 12
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontLarge
            color: Theme.text
            text: qsTr("Move %1 of %2 pictures to %3?").arg(root.count).arg(root.total).arg(root.target)
        }
        Label {
            Layout.fillWidth: true
            visible: root.conflicts > 0
            wrapMode: Text.Wrap
            color: Theme.textDim
            text: root.conflicts === 1 ? qsTr("1 of them already exists there; you will be asked what to do.")
                  : qsTr("%1 of them already exist there; you will be asked what to do.").arg(root.conflicts)
        }
    }

    footer: DialogButtonBox {
        Button {
            id: yesButton
            text: qsTr("Yes, move")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            Keys.onReturnPressed: clicked()
            Keys.onEnterPressed: clicked()
        }
        Button {
            id: noButton
            text: qsTr("No")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            Keys.onReturnPressed: clicked()
            Keys.onEnterPressed: clicked()
        }
    }

    onAccepted: root.confirmed()
}
