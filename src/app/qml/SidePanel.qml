// SPDX-License-Identifier: GPL-3.0-or-later
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
    signal cameraClocksRequested()
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
        currentFolder: App.fileUrl(root.session.targetDir)
        onAccepted: { root.session.setTargetDir(selectedFolder.toString()); root.activated() }
        onRejected: root.activated()
    }

    component FieldLabel: Label {
        color: Theme.textDim
        font.pixelSize: Theme.fontSmall
        Layout.alignment: Qt.AlignVCenter
    }

    component SmallButton: Button {
        focusPolicy: Qt.NoFocus
        font.pixelSize: Theme.fontSmall
        topPadding: 3
        bottomPadding: 3
        leftPadding: 8
        rightPadding: 8
    }

    component SmallCombo: ComboBox {
        focusPolicy: Qt.NoFocus
        font.pixelSize: Theme.fontSmall
        Layout.fillWidth: true
        implicitHeight: 28
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 8

        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: content.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            readonly property bool overflowing: contentHeight > height + 1
            // Always visible when there is more to see, so it's obvious.
            ScrollBar.vertical: ScrollBar {
                id: vbar
                policy: flick.overflowing ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
            }

            ColumnLayout {
                id: content
                width: flick.width - (flick.overflowing ? vbar.width + 6 : 0)
                spacing: 6

                // Folders and folder settings, packed into one aligned grid.
                GridLayout {
                    Layout.fillWidth: true
                    columns: 3
                    columnSpacing: 8
                    rowSpacing: 6

                    FieldLabel { text: qsTr("From") }
                    PathLabel { text: root.session.sourceDir; Layout.columnSpan: 2; font.pixelSize: Theme.fontSmall }

                    FieldLabel { text: qsTr("To") }
                    PathLabel { text: root.session.targetDir; font.pixelSize: Theme.fontSmall }
                    SmallButton { text: qsTr("Change…"); onClicked: targetPicker.open() }

                    FieldLabel { text: qsTr("New files") }
                    SmallCombo {
                        Layout.columnSpan: 2
                        model: [qsTr("start not selected"), qsTr("start selected")]
                        currentIndex: root.session.defaultSelected ? 1 : 0
                        onActivated: index => { root.session.defaultSelected = index === 1; root.activated() }
                    }

                    FieldLabel { text: qsTr("Sort by") }
                    SmallCombo {
                        model: [qsTr("name"), qsTr("date taken")]
                        currentIndex: root.session.sortMode === "date" ? 1 : 0
                        onActivated: index => { root.session.sortMode = index === 1 ? "date" : "name"; root.activated() }
                    }
                    SmallButton { text: qsTr("Clocks…"); onClicked: root.cameraClocksRequested() }
                }

                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border; Layout.topMargin: 4 }

                RowLayout {
                    Layout.fillWidth: true
                    SectionTitle { text: qsTr("Picture %1 of %2").arg(root.currentIndex + 1).arg(root.session.totalCount); Layout.fillWidth: true }
                    SmallButton {
                        text: root.session.showDetails ? qsTr("Fewer details ▴") : qsTr("More details ▾")
                        flat: true
                        onClicked: { root.session.showDetails = !root.session.showDetails; root.activated() }
                    }
                }
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
                InfoRow { key: qsTr("Duration"); value: root.info.duration ?? "" }
                InfoRow {
                    key: qsTr("Similar")
                    value: (root.info.groupSize ?? 0) > 1 ? qsTr("one of %1 alike pictures").arg(root.info.groupSize) : ""
                }
                // EXIF details, hidden by default.
                InfoRow { key: qsTr("Taken"); value: root.session.showDetails ? (root.info.date ?? "") : "" }
                InfoRow { key: qsTr("Camera"); value: root.session.showDetails ? (root.info.camera ?? "") : "" }
                InfoRow { key: qsTr("Lens"); value: root.session.showDetails ? (root.info.lens ?? "") : "" }
                InfoRow { key: qsTr("Exposure"); value: root.session.showDetails ? (root.info.exposure ?? "") : "" }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    spacing: 8
                    Button {
                        Layout.fillWidth: true
                        text: qsTr("✓ Select (S)")
                        focusPolicy: Qt.NoFocus
                        highlighted: root.info.selected === true
                        onClicked: { root.selectRequested(); root.activated() }
                    }
                    Button {
                        Layout.fillWidth: true
                        text: qsTr("✗ Deselect (D)")
                        focusPolicy: Qt.NoFocus
                        highlighted: root.info.selected === false
                        onClicked: { root.deselectRequested(); root.activated() }
                    }
                }

                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border; Layout.topMargin: 6 }

                RowLayout {
                    Layout.fillWidth: true
                    SectionTitle { text: qsTr("Similar pictures"); Layout.fillWidth: true }
                    Switch {
                        focusPolicy: Qt.NoFocus
                        checked: root.session.showSimilar
                        onToggled: { root.session.showSimilar = checked; root.activated() }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    visible: root.session.showSimilar
                    wrapMode: Text.Wrap
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSmall
                    text: root.session.groupCount === 0 ? qsTr("No groups of alike pictures found.")
                          : root.session.groupCount === 1 ? qsTr("1 group, marked by a coloured bar under its pictures.")
                          : qsTr("%1 groups, each marked by a coloured bar under its pictures.").arg(root.session.groupCount)
                }
                GridLayout {
                    Layout.fillWidth: true
                    visible: root.session.showSimilar
                    columns: 3
                    columnSpacing: 8
                    Label { text: qsTr("Within"); color: Theme.textDim; font.pixelSize: Theme.fontSmall }
                    Slider {
                        Layout.fillWidth: true
                        focusPolicy: Qt.NoFocus
                        from: 1; to: 60; stepSize: 1
                        value: root.session.similarSeconds
                        onMoved: root.session.similarSeconds = value
                        onPressedChanged: if (!pressed) root.activated()
                    }
                    Label {
                        text: qsTr("%1 s").arg(root.session.similarSeconds)
                        color: Theme.text; font.pixelSize: Theme.fontSmall
                        Layout.preferredWidth: 40
                    }
                    Label { text: qsTr("Alike"); color: Theme.textDim; font.pixelSize: Theme.fontSmall }
                    Slider {
                        Layout.fillWidth: true
                        focusPolicy: Qt.NoFocus
                        // Left = strict (few differing bits), right = loose.
                        from: 2; to: 24; stepSize: 1
                        value: root.session.similarBits
                        onMoved: root.session.similarBits = value
                        onPressedChanged: if (!pressed) root.activated()
                    }
                    Label {
                        text: root.session.similarBits <= 6 ? qsTr("strict")
                              : root.session.similarBits <= 14 ? qsTr("normal") : qsTr("loose")
                        color: Theme.text; font.pixelSize: Theme.fontSmall
                        Layout.preferredWidth: 40
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
                KeyRow { keys: "Space / M"; action: qsTr("video: play/pause, sound") }
                KeyRow { keys: "+ / −"; action: qsTr("bigger / smaller thumbnails") }
                KeyRow { keys: "Ctrl+Z"; action: qsTr("undo") }
            }
        }

        // "More below" hint over the bottom of the scrolling area.
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 0
            visible: flick.overflowing && !flick.atYEnd
            z: 2
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.top
                height: 36
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 1.0; color: Theme.panel }
                }
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    text: qsTr("more below ▾")
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSmall
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: flick.contentY = flick.contentHeight - flick.height
                }
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
            text: root.session.selectedCount === 1 ? qsTr("Move 1 picture…")
                  : qsTr("Move %1 pictures…").arg(root.session.selectedCount)
            font.pixelSize: Theme.fontLarge
            font.bold: true
            onClicked: root.moveRequested()
            contentItem: Label {
                text: moveButton.text
                font: moveButton.font
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: moveButton.enabled ? "white" : Theme.textDim
            }
            background: Rectangle {
                radius: 6
                color: !moveButton.enabled ? Theme.panelRaised
                       : moveButton.down ? Qt.darker(Theme.accent, 1.2)
                       : moveButton.hovered ? Qt.lighter(Theme.accent, 1.1) : Theme.accent
            }
        }
    }
}
