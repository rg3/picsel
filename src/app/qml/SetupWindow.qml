import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Picsel

// First time a folder is opened: choose the destination and the default state.
ApplicationWindow {
    id: root

    required property string sourceDir
    property string targetDir: ""
    property string highlight: ""
    property bool done: false

    readonly property bool targetValid: targetDir !== "" && App.isWritableDir(targetDir)
                                        && targetDir !== sourceDir

    width: 640
    height: layout.implicitHeight + 48
    minimumWidth: 520
    visible: true
    title: qsTr("New selection — picsel")
    color: Theme.panel
    palette: Theme.palette

    function accept() {
        if (!targetValid)
            return
        done = true
        App.setupAccepted(sourceDir, targetDir, selectedRadio.checked, highlight)
        close()
    }

    onClosing: if (!done) { done = true; App.setupCancelled() }

    FolderDialog {
        id: picker
        title: qsTr("Folder for the selected pictures")
        currentFolder: "file://" + (root.targetDir !== "" ? root.targetDir : root.sourceDir)
        onAccepted: root.targetDir = App.toLocalPath(selectedFolder.toString())
    }

    Shortcut { sequences: [StandardKey.Cancel]; onActivated: root.close() }

    ColumnLayout {
        id: layout
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 24
        spacing: 12

        Label {
            text: qsTr("Start selecting pictures in")
            color: Theme.textDim
        }
        Label {
            Layout.fillWidth: true
            text: root.sourceDir
            color: Theme.text
            font.pixelSize: Theme.fontLarge
            font.bold: true
            elide: Text.ElideLeft
        }

        Label {
            Layout.topMargin: 8
            text: qsTr("Move the selected pictures to")
            color: Theme.textDim
        }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                Layout.fillWidth: true
                text: root.targetDir
                placeholderText: qsTr("Choose a folder…")
                onTextEdited: root.targetDir = text
                onAccepted: root.accept()
            }
            Button { text: qsTr("Browse…"); onClicked: picker.open() }
        }
        Label {
            visible: !root.targetValid
            text: root.targetDir === "" ? qsTr("Choose where selected pictures will go.")
                  : root.targetDir === root.sourceDir ? qsTr("The destination must differ from the source folder.")
                  : qsTr("That folder doesn't exist or isn't writable.")
            color: Theme.danger
            font.pixelSize: Theme.fontSmall
        }

        Label {
            Layout.topMargin: 8
            text: qsTr("Pictures start as")
            color: Theme.textDim
        }
        RowLayout {
            RadioButton {
                id: deselectedRadio
                checked: true
                text: qsTr("Not selected — I'll pick the keepers")
            }
            RadioButton {
                id: selectedRadio
                text: qsTr("Selected — I'll pick the rejects")
            }
        }

        RowLayout {
            Layout.topMargin: 12
            Layout.alignment: Qt.AlignRight
            Button { text: qsTr("Cancel"); onClicked: root.close() }
            Button {
                text: qsTr("Start")
                highlighted: true
                enabled: root.targetValid
                onClicked: root.accept()
            }
        }
    }
}
