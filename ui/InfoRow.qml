import QtQuick
import QtQuick.Layouts

RowLayout {
    property string label: ""
    property string value: ""
    implicitHeight: 36
    spacing: 16
    AppText { text: parent.label; color: "#738096"; Layout.preferredWidth: 114 }
    AppText { text: parent.value; horizontalAlignment: Text.AlignRight; elide: Text.ElideRight; Layout.fillWidth: true }
}
