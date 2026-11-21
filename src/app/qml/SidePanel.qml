import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Picsel

// Left panel: folders, defaults, info about the highlighted picture, mouse
// alternatives to the keyboard, and the final "move" action.
Rectangle {
    id: root

    required property Session session
    property int currentIndex: 0
    property var info: ({})

    signal selectRequested()
    signal deselectRequested()
    signal moveRequested()
    signal activated()   // a control was used; give focus back to the mosaic

    color: Theme.panel

    function refreshInfo() { info = session.info(currentIndex) }
    onCurrentIndexChanged: refreshInfo()
    Component.onCompleted: refreshInfo()

    Connections {
        target: root.session
        function onItemInfoChanged(row) { if (row === root.currentIndex) root.refreshInfo() }
        function onCountsChanged() { root.refreshInfo() }
    }

    component SectionTitle: Label {
        color: Theme.textDim
        font.pixelSize: Theme.fontSmall
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 1
        topPadding: 6
    }

    component PathLabel: Label {
        Layout.fillWidth: true
        color: Theme.text
        font.pixelSize: Theme.fontNormal
        elide: Text.ElideLeft
        ToolTip.visible: hover.hovered && truncated
        ToolTip.text: text
        HoverHandler { id: hover }
    }

    component InfoRow: RowLayout {
        property string key
        property string value
        visible: value !== ""
        Layout.fillWidth: true
        Label { text: key; color: Theme.textDim; font.pixelSize: Theme.fontNormal; Layout.preferredWidth: 96 }
        Label {
            text: value; color: Theme.text; font.pixelSize: Theme.fontNormal
            Layout.fillWidth: true; elide: Text.ElideMiddle
        }
    }

    component KeyRow: RowLayout {
        property string keys
        property string action
        Layout.fillWidth: true
        spacing: 10
        Rectangle {
            Layout.preferredWidth: 78
            implicitHeight: keyText.implicitHeight + 6
            radius: 4
            color: Theme.panelRaised
            border.color: Theme.border
            Label {
                id: keyText
                anchors.centerIn: parent
                text: keys
                color: Theme.text
                font.pixelSize: Theme.fontSmall
            }
        }
        Label { text: action; color: Theme.textDim; font.pixelSize: Theme.fontSmall; Layout.fillWidth: true }
    }

    FolderDialog {
        id: targetPicker
        title: qsTr("Folder for the selected pictures")
        currentFolder: "file://" + root.session.targetDir
        onAccepted: { root.session.setTargetDir(selectedFolder.toString()); root.activated() }
        onRejected: root.activated()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 8

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: content.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            ColumnLayout {
                id: content
                width: parent.width
                spacing: 8

                SectionTitle { text: qsTr("Source") }
                PathLabel { text: root.session.sourceDir }

                SectionTitle { text: qsTr("Move selected to") }
                RowLayout {
                    Layout.fillWidth: true
                    PathLabel { text: root.session.targetDir }
                    Button {
                        text: qsTr("Change…")
                        focusPolicy: Qt.NoFocus
                        onClicked: targetPicker.open()
                    }
                }

                SectionTitle { text: qsTr("New pictures start as") }
                RowLayout {
                    Layout.fillWidth: true
                    RadioButton {
                        text: qsTr("Selected")
                        focusPolicy: Qt.NoFocus
                        checked: root.session.defaultSelected
                        onClicked: { root.session.defaultSelected = true; root.activated() }
                    }
                    RadioButton {
                        text: qsTr("Not selected")
                        focusPolicy: Qt.NoFocus
                        checked: !root.session.defaultSelected
                        onClicked: { root.session.defaultSelected = false; root.activated() }
                    }
                }

                SectionTitle { text: qsTr("Sort by") }
                RowLayout {
                    Layout.fillWidth: true
                    RadioButton {
                        text: qsTr("Name")
                        focusPolicy: Qt.NoFocus
                        checked: root.session.sortMode === "name"
                        onClicked: { root.session.sortMode = "name"; root.activated() }
                    }
                    RadioButton {
                        text: qsTr("Date taken")
                        focusPolicy: Qt.NoFocus
                        checked: root.session.sortMode === "date"
                        onClicked: { root.session.sortMode = "date"; root.activated() }
                    }
                }

                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border; Layout.topMargin: 6 }

                SectionTitle { text: qsTr("Picture %1 of %2").arg(root.currentIndex + 1).arg(root.session.totalCount) }
                Label {
                    Layout.fillWidth: true
                    text: root.info.name ?? ""
                    color: Theme.text
                    font.pixelSize: Theme.fontLarge
                    font.bold: true
                    elide: Text.ElideMiddle
                }
                InfoRow { key: qsTr("Size"); value: root.info.size ?? "" }
                InfoRow {
                    key: qsTr("Dimensions")
                    value: (root.info.width ?? 0) > 0 ? root.info.width + " × " + root.info.height : ""
                }
                InfoRow { key: qsTr("Taken"); value: root.info.date ?? "" }
                InfoRow { key: qsTr("Camera"); value: root.info.camera ?? "" }
                InfoRow { key: qsTr("Lens"); value: root.info.lens ?? "" }
                InfoRow { key: qsTr("Exposure"); value: root.info.exposure ?? "" }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    spacing: 8
                    Button {
                        Layout.fillWidth: true
                        text: qsTr("✓ Select")
                        focusPolicy: Qt.NoFocus
                        highlighted: root.info.selected === true
                        onClicked: { root.selectRequested(); root.activated() }
                    }
                    Button {
                        Layout.fillWidth: true
                        text: qsTr("✗ Deselect")
                        focusPolicy: Qt.NoFocus
                        highlighted: root.info.selected === false
                        onClicked: { root.deselectRequested(); root.activated() }
                    }
                }

                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border; Layout.topMargin: 6 }

                SectionTitle { text: qsTr("Keys") }
                KeyRow { keys: "S / D"; action: qsTr("select / deselect, then next") }
                KeyRow { keys: "T"; action: qsTr("toggle selection") }
                KeyRow { keys: "← → ↑ ↓"; action: qsTr("move") }
                KeyRow { keys: "Home / End"; action: qsTr("first / last") }
                KeyRow { keys: "PgUp / PgDn"; action: qsTr("page up / down") }
                KeyRow { keys: "Enter"; action: qsTr("full view (Esc closes)") }
                KeyRow { keys: "+ / −"; action: qsTr("bigger / smaller thumbnails") }
                KeyRow { keys: "Ctrl+Z"; action: qsTr("undo") }
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }

        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("%1 of %2 selected").arg(root.session.selectedCount).arg(root.session.totalCount)
            color: Theme.text
            font.pixelSize: Theme.fontLarge
        }

        Button {
            id: moveButton
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            focusPolicy: Qt.NoFocus
            enabled: root.session.selectedCount > 0
            text: qsTr("Move %n selected…", "", root.session.selectedCount)
            font.pixelSize: Theme.fontLarge
            font.bold: true
            palette.button: Theme.accent
            palette.buttonText: "white"
            onClicked: root.moveRequested()
        }
    }
}
