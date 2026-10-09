import QtQuick

Rectangle {
    id: panel
    property int padding: 24
    default property alias content: body.data
    radius: 20
    color: "#ffffff"
    border.width: 0
    Item { id: body; anchors.fill: parent; anchors.margins: panel.padding }
}
