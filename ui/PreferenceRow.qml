import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: row
    property string label: ""
    property bool checked: false
    signal toggled(bool value)
    implicitHeight: 44
    AppText { text: row.label; color: row.enabled ? "#526078" : "#a2a9b7"; Layout.fillWidth: true }
    Switch {
        id: control
        checked: row.checked
        Layout.preferredWidth: 48
        Layout.preferredHeight: 32
        onToggled: {
            row.toggled(checked)
            // Re-read the accepted value so a failed save never leaves the switch enabled visually.
            checked = Qt.binding(function() { return row.checked })
        }
        indicator: Rectangle {
            width: 46; height: 26; radius: 13; y: 3
            color: !control.enabled ? "#e3e7ee" : control.checked ? "#4057d6" : "#c6cdd8"
            Rectangle { width: 20; height: 20; radius: 10; y: 3; x: control.checked ? 23 : 3; color: "white"; Behavior on x { NumberAnimation { duration: 120 } } }
        }
        Accessible.name: row.label
    }
}
