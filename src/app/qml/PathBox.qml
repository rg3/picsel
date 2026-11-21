import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Picsel

// A folder path on its own highlighted line.
Rectangle {
    property alias text: label.text
    Layout.fillWidth: true
    implicitHeight: label.implicitHeight + 20
    radius: 6
    color: Theme.panelRaised
    border.color: Theme.border
    Label {
        id: label
        anchors.fill: parent
        anchors.margins: 10
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        color: "#9cc2ff"
        font.pixelSize: Theme.fontLarge
        font.bold: true
    }
}
