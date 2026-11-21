import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Picsel

ApplicationWindow {
    id: root

    required property string message
    property bool done: false

    width: 520
    height: layout.implicitHeight + 48
    visible: true
    title: qsTr("picsel")
    color: Theme.panel
    palette: Theme.palette

    onClosing: if (!done) { done = true; App.dialogClosed() }
    Shortcut { sequences: [StandardKey.Cancel, "Return"]; onActivated: root.close() }

    ColumnLayout {
        id: layout
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 24
        spacing: 16
        Label {
            Layout.fillWidth: true
            text: root.message
            wrapMode: Text.Wrap
            color: Theme.text
            font.pixelSize: Theme.fontNormal
        }
        Button {
            Layout.alignment: Qt.AlignRight
            text: qsTr("Close")
            onClicked: root.close()
        }
    }
}
