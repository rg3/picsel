// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import Picsel

// Shown when picsel starts without a path (e.g. from the app menu).
//
// With the desktop portal (portalDialog), the desktop shows its own dialog
// and this window stays hidden. Otherwise Qt Quick draws its own dialog,
// which needs a parent window (a parentless one is never shown): this
// window, with the dialog drawn inside it rather than as a second window.
ApplicationWindow {
    id: root

    required property bool portalDialog
    property bool done: false
    signal finished // C++ deletes the window

    width: 640
    height: 440
    visible: !portalDialog
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
        finished()
    }

    FolderDialog {
        id: dialog
        parentWindow: root
        title: qsTr("Choose a folder of pictures")
        onAccepted: {
            if (selectedFolder.toString() === "")
                console.warn("folder dialog accepted without a folder")
            root.finish(selectedFolder.toString())
        }
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
