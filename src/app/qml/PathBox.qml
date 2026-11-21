import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Picsel

// A folder path on its own highlighted line. Long paths drop their start
// (the end is what identifies the folder); hover shows the full path.
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
        elide: Text.ElideLeft
        color: "#9cc2ff"
        font.pixelSize: Theme.fontLarge
        font.bold: true
        ToolTip.visible: hover.hovered && truncated
        ToolTip.text: text
        ToolTip.delay: 300
        HoverHandler { id: hover }
    }
}
