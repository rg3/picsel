// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import Picsel

// Shown when picsel starts without a path (e.g. from the app menu).
// FolderDialog needs a window: without a native dialog (KDE's needs a
// QtWidgets app) Qt Quick draws its own one for a parent window, and a
// parentless one is never shown. It is drawn inside this window rather than
// as a second window next to an empty one.
ApplicationWindow {
    id: root

    property bool done: false

    width: 640
    height: 440
    visible: true
    title: qsTr("Choose a folder of pictures — picsel")
    color: Theme.panel
    palette: Theme.palette

    function finish(url) {
        if (done)
            return
        done = true
        if (url !== "")
            App.openFolder(url)
        else
            App.dialogClosed()
        close()
    }

    FolderDialog {
        id: dialog
        parentWindow: root
        title: qsTr("Choose a folder of pictures")
        onAccepted: root.finish(selectedFolder.toString())
        onRejected: root.finish("")
    }

    Component.onCompleted: {
        // Qt >= 6.10 defaults to Popup.Window; the property is missing before.
        if ("popupType" in dialog)
            dialog.popupType = Popup.Item
        // Opened right away, the dialog finds no window yet.
        Qt.callLater(dialog.open)
    }
    onClosing: finish("")
}
