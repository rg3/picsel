// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Dialogs
import Picsel

// Shown when picsel starts without a path.
FolderDialog {
    title: qsTr("picsel — choose a folder of pictures")
    onAccepted: App.openFolder(selectedFolder.toString())
    onRejected: App.dialogClosed()
    Component.onCompleted: open()
}
