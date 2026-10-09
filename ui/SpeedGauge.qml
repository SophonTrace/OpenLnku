import QtQuick
Item {
    id: gauge
    property real value: 0
    property string phase: "准备测速"
    property bool complete: false
    readonly property real ceiling: value <= 600 ? 600 : Math.ceil(value / 600) * 600
    Canvas {
        id: arc
        anchors.fill: parent
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        Connections { target: gauge; function onValueChanged() { arc.requestPaint() } }
        onPaint: {
            const c = getContext("2d"); c.reset()
            const r = Math.min(width / 2 - 43, height - 67), cx = width / 2, cy = height - 44
            if (r <= 0) return
            c.lineWidth = 10; c.lineCap = "round"
            c.strokeStyle = "#dfedff"; c.beginPath(); c.arc(cx, cy, r, Math.PI, Math.PI * 2); c.stroke()
            const end = Math.PI + Math.PI * Math.min(1, gauge.value / gauge.ceiling)
            if (gauge.value > 0) {
                const gradient = c.createLinearGradient(cx-r, cy-r, cx+r, cy)
                gradient.addColorStop(0, "#4086ff"); gradient.addColorStop(1, "#184cff")
                c.strokeStyle = gradient; c.beginPath(); c.arc(cx, cy, r, Math.PI, end); c.stroke()
            }
            c.lineWidth = 1; c.font = "10px 'Segoe UI'"; c.textAlign = "center"; c.textBaseline = "middle"
            for (let i = 0; i <= 6; i++) {
                const a = Math.PI + Math.PI * i / 6
                c.strokeStyle = i % 2 ? "#cbdcf9" : "#6b9cff"
                c.beginPath(); c.moveTo(cx + Math.cos(a) * (r - 11), cy + Math.sin(a) * (r - 11)); c.lineTo(cx + Math.cos(a) * (r - 18), cy + Math.sin(a) * (r - 18)); c.stroke()
                c.fillStyle = "#7483a5"
                c.fillText(String(Math.round(i * gauge.ceiling / 6)), cx + Math.cos(a) * (r + 19), cy + Math.sin(a) * (r + 19))
            }
            if (gauge.value > 0) { c.fillStyle = "#184cff"; c.strokeStyle = "white"; c.lineWidth = 2.5; c.beginPath(); c.arc(cx + Math.cos(end) * r, cy + Math.sin(end) * r, 5, 0, Math.PI * 2); c.fill(); c.stroke() }
        }
    }
    Column {
        anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: 1
        spacing: 1
        AppText { anchors.horizontalCenter: parent.horizontalCenter; text: gauge.value > 0 ? gauge.value.toFixed(1) : "—"; font.family: "Segoe UI"; font.pixelSize: 33; font.weight: Font.Bold; color: "#080e36" }
        AppText { anchors.horizontalCenter: parent.horizontalCenter; text: "Mbps"; font.pixelSize: 10; color: "#7483a5" }
        AppText { anchors.horizontalCenter: parent.horizontalCenter; text: gauge.phase.indexOf("上传") >= 0 ? "上传速度" : "下载速度"; font.pixelSize: 11; color: "#080e36" }
        Rectangle {
            width: Math.max(80, stateText.implicitWidth + 32); height: 23; radius: 12
            anchors.horizontalCenter: parent.horizontalCenter; color: gauge.complete ? "#e5f5ed" : "#edf3ff"
            Row { anchors.centerIn: parent; spacing: 5
                Rectangle { width: 11; height: 11; radius: 6; color: "#008d55"; visible: gauge.complete; anchors.verticalCenter: parent.verticalCenter; AppIcon { anchors.fill: parent; anchors.margins: 1; kind: "check"; ink: "white" } }
                AppText { id: stateText; text: gauge.complete ? "测速完成" : gauge.phase; color: gauge.complete ? "#008d55" : "#245ac0"; font.pixelSize: 9; font.weight: Font.DemiBold }
            }
        }
    }
}
