import QtQuick
import QtQuick.Controls

ComboBox {
    id: control
    property color fill: "white"
    implicitHeight: 44
    implicitWidth: 180
    leftPadding: 14
    rightPadding: 38
    background: Rectangle {
        radius: 10
        color: control.enabled ? control.fill : "#f7f8fb"
        border.color: control.activeFocus ? "#4057d6" : "#dce1eb"
    }
    contentItem: AppText {
        text: control.displayText
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        color: control.enabled ? "#17203b" : "#a2a9b7"
    }
    indicator: Canvas {
        x: control.width - width - 15
        y: (control.height - height) / 2
        width: 12; height: 8
        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.strokeStyle = "#526078"
            ctx.lineWidth = 1.5
            ctx.lineCap = "round"
            ctx.beginPath(); ctx.moveTo(1, 1); ctx.lineTo(6, 6); ctx.lineTo(11, 1); ctx.stroke()
        }
    }
    delegate: ItemDelegate {
        required property int index
        width: control.width - 12
        height: 42
        highlighted: control.highlightedIndex === index
        contentItem: AppText {
            text: control.textAt(index)
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle { radius: 7; color: parent.highlighted ? "#eef1ff" : "transparent" }
    }
    popup: Popup {
        y: control.height + 5
        width: control.width
        padding: 6
        implicitHeight: Math.min(contentItem.implicitHeight + 12, 250)
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
        background: Rectangle { color: "white"; radius: 11; border.color: "#dce1eb" }
    }
}
