import QtQuick
import QtQuick.Controls

TextField {
    id: control
    implicitHeight: 43
    color: "#17203b"
    placeholderTextColor: "#9ba3b4"
    font.pixelSize: 14
    font.family: "Segoe UI"
    leftPadding: 14
    rightPadding: 14
    selectByMouse: true
    background: Rectangle {
        radius: 9
        color: control.enabled ? "#ffffff" : "#f7f8fb"
        border.color: control.activeFocus ? "#2c60d4" : "#dce1eb"
        border.width: 1
    }
}
