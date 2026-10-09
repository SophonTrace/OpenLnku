import QtQuick
import QtQuick.Controls

Button {
    id: control
    property bool primary: true
    property color accent: "#245ac0"
    implicitHeight: 44
    implicitWidth: Math.max(104, contentItem.implicitWidth + 38)
    font.pixelSize: 14
    font.weight: Font.DemiBold
    hoverEnabled: true
    background: Rectangle {
        radius: 12
        color: !control.enabled ? "#eef0f5" : control.primary ? (control.down ? Qt.darker(control.accent, 1.15) : control.hovered ? Qt.lighter(control.accent, 1.1) : control.accent) : (control.hovered ? "#edf3ff" : "#f5f8ff")
        border.color: control.primary ? "transparent" : "#e2e5ed"
        Behavior on color { ColorAnimation { duration: 100 } }
    }
    contentItem: Text {
        text: control.text
        font: control.font
        color: !control.enabled ? "#9ca3af" : control.primary ? "#ffffff" : "#245ac0"
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    Accessible.name: text
}
