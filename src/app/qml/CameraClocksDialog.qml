import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Picsel

// Per-camera clock corrections (global), used when sorting by date so that
// DSLR and phone pictures interleave correctly.
Dialog {
    id: root

    required property Session session
    property var rows: []

    anchors.centerIn: Overlay.overlay
    modal: true
    title: qsTr("Camera clocks")
    width: Math.min(760, parent.width - 80)
    standardButtons: Dialog.Close

    onAboutToShow: rows = session.cameras()

    function format(seconds) {
        const sign = seconds < 0 ? "-" : "+"
        let s = Math.abs(seconds)
        const h = Math.floor(s / 3600); s -= h * 3600
        const m = Math.floor(s / 60); s -= m * 60
        return sign + h + ":" + String(m).padStart(2, "0") + ":" + String(s).padStart(2, "0")
    }

    function parse(text) {
        const m = /^\s*([+-]?)(\d{1,3}):([0-5]?\d):([0-5]?\d)\s*$/.exec(text)
        if (!m)
            return null
        const v = Number(m[2]) * 3600 + Number(m[3]) * 60 + Number(m[4])
        return m[1] === "-" ? -v : v
    }

    ColumnLayout {
        width: parent.width
        spacing: 12

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.textDim
            text: qsTr("If a camera's clock was off, enter how much to add to its times (±h:mm:ss). "
                       + "Corrections apply to every folder and to sorting by date.")
        }

        Label {
            visible: root.rows.length === 0
            color: Theme.text
            text: qsTr("No camera information found in this folder yet.")
        }

        Repeater {
            model: root.rows
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 12
                Label {
                    Layout.fillWidth: true
                    text: modelData.camera
                    color: Theme.text
                    font.pixelSize: Theme.fontLarge
                    elide: Text.ElideRight
                }
                Label {
                    text: qsTr("%1 files").arg(modelData.count)
                    color: Theme.textDim
                }
                TextField {
                    id: field
                    Layout.preferredWidth: 140
                    text: root.format(modelData.offset)
                    horizontalAlignment: Text.AlignHCenter
                    color: root.parse(text) === null ? Theme.danger : Theme.text
                    onEditingFinished: {
                        const v = root.parse(text)
                        if (v !== null && v !== modelData.offset)
                            root.session.setCameraOffset(modelData.camera, v)
                    }
                }
                Button {
                    text: qsTr("Reset")
                    enabled: root.parse(field.text) !== 0
                    onClicked: {
                        field.text = root.format(0)
                        root.session.setCameraOffset(modelData.camera, 0)
                    }
                }
            }
        }
    }
}
