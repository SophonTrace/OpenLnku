import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Panel {
    id: card
    padding: 18
    implicitHeight: speed.error.length ? 464 : 438
    function number(value) { return value < 0 ? "—" : value.toFixed(0) }
    ColumnLayout {
        anchors.fill: parent; spacing: 7
        RowLayout {
            Layout.fillWidth: true; Layout.preferredHeight: 40
            AppIcon { kind: "speed"; ink: "#184cff"; width: 32; height: 32 }
            AppText { text: "网络测速"; color: "#080e36"; font.pixelSize: 21; font.weight: Font.Bold; Layout.leftMargin: 6 }
            Item { Layout.fillWidth: true }
            ActionButton { text: speed.running ? "取消测速" : "开始测速"; accent: "#1b59d7"; implicitWidth: 122; implicitHeight: 41; onClicked: speed.running ? speed.cancel() : speed.start(client.interfaceIndex(), client.ip, client.adapterLabel) }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 12; Layout.preferredHeight: 33
            Rectangle {
                Layout.preferredWidth: 280; Layout.preferredHeight: 33; radius: 9; color: "white"; border.color: "#d5ddec"
                RowLayout {
                    anchors.fill: parent; anchors.margins: 2; spacing: 2
                    Repeater {
                        model: ["系统路由", "实体网卡直连"]
                        Button {
                            required property int index; required property string modelData
                            Layout.fillWidth: true; Layout.fillHeight: true; enabled: !speed.running
                            onClicked: speed.mode = index
                            background: Rectangle { radius: 8; color: speed.mode === parent.index ? "#e9eeff" : "transparent" }
                            contentItem: AppText { text: parent.modelData; color: speed.mode === parent.index ? "#184cff" : "#6e7d9d"; font.pixelSize: 12; font.weight: speed.mode === parent.index ? Font.Bold : Font.Normal; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        }
                    }
                }
            }
            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 22; color: "#dce2ec"; Layout.leftMargin: 7; Layout.rightMargin: 7 }
            AppText { text: "出口 IP"; color: "#7483a5"; font.pixelSize: 12 }
            AppText { text: speed.exitIp; color: "#080e36"; font.family: "Segoe UI"; font.pixelSize: 14; elide: Text.ElideMiddle; Layout.maximumWidth: 125 }
            Button {
                implicitWidth: 22; implicitHeight: 24; padding: 0; onClicked: speed.copyExitIp(); enabled: speed.exitIp !== "—"
                background: Rectangle { radius: 6; color: parent.hovered ? "#edf2ff" : "transparent" }
                contentItem: AppIcon { kind: "copy"; ink: "#080e36" }
                Accessible.name: "复制出口 IP"
            }
            Rectangle {
                Layout.preferredWidth: Math.min(104, routeText.implicitWidth + 24); Layout.preferredHeight: 27; radius: 8; color: "#edf4ff"
                AppText { id: routeText; anchors.centerIn: parent; text: speed.mode === 0 ? client.demo ? "Mihomo TUN" : client.routeName : client.adapterLabel; width: Math.min(84, implicitWidth); elide: Text.ElideRight; color: "#647695"; font.pixelSize: 11 }
            }
            Item { Layout.fillWidth: true }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.preferredHeight: 171; Layout.minimumHeight: 171; Layout.maximumHeight: 171; spacing: 14
            SpeedGauge { Layout.preferredWidth: 304; Layout.fillHeight: true; value: speed.phase.indexOf("上传") >= 0 ? speed.upload : speed.download; phase: speed.phase; complete: speed.complete }
            GridLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; columns: 2; columnSpacing: 8; rowSpacing: 8
                Repeater {
                    model: [
                        {label:"下载", icon:"downloadArrow", value:speed.download > 0 ? speed.download.toFixed(1) : "—", unit:"Mbps", tint:"#245bd4", bg:"#eaf3ff"},
                        {label:"上传", icon:"uploadArrow", value:speed.upload > 0 ? speed.upload.toFixed(1) : "—", unit:"Mbps", tint:"#008d55", bg:"#e5f5ed"},
                        {label:"HTTP 延迟", icon:"clock", value:card.number(speed.latency), unit:"ms", tint:"#080e36", bg:"#f0f3ff"},
                        {label:"抖动", icon:"heartbeat", value:card.number(speed.jitter), unit:"ms", tint:"#080e36", bg:"#f0f3ff"}
                    ]
                    Rectangle {
                        required property var modelData
                        Layout.fillWidth: true; Layout.fillHeight: true; radius: 9; color: "white"; border.color: "#edf0f7"
                        RowLayout {
                            anchors.fill: parent; anchors.margins: 12; spacing: 12
                            Rectangle { Layout.preferredWidth: 43; Layout.preferredHeight: 43; radius: 22; color: modelData.bg; AppIcon { anchors.centerIn: parent; kind: modelData.icon; ink: modelData.tint; width: 25; height: 25 } }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 1
                                AppText { text: modelData.label; font.pixelSize: 12; color: "#7483a5" }
                                RowLayout { spacing: 4; AppText { text: modelData.value; color: "#080e36"; font.family: "Segoe UI"; font.pixelSize: 24; font.weight: Font.Bold } AppText { text: modelData.unit; color: "#080e36"; font.family: "Segoe UI"; font.pixelSize: 12; Layout.alignment: Qt.AlignBaseline } }
                            }
                        }
                    }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 101; radius: 10; color: "#f5f9ff"
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 13; spacing: 3
                RowLayout {
                    spacing: 14
                    AppText { text: "Mbps"; color: "#7483a5"; font.pixelSize: 11 }
                    Row { spacing: 6; Rectangle { width: 7; height: 7; radius: 4; color: "#3475ff"; anchors.verticalCenter: parent.verticalCenter } AppText { text: "下载"; color: "#7483a5"; font.pixelSize: 11 } }
                    Row { spacing: 6; Rectangle { width: 7; height: 7; radius: 4; color: "#009d98"; anchors.verticalCenter: parent.verticalCenter } AppText { text: "上传"; color: "#7483a5"; font.pixelSize: 11 } }
                    Item { Layout.fillWidth: true }
                }
                SpeedChart { Layout.fillWidth: true; Layout.fillHeight: true; samples: speed.history }
            }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#edf0f6" }
        RowLayout {
            Layout.fillWidth: true; Layout.preferredHeight: 21; spacing: 13
            AppText { text: "节点"; color: "#7483a5"; font.pixelSize: 11 }
            AppText { text: speed.node; color: "#080e36"; font.pixelSize: 12 }
            Item { Layout.fillWidth: true }
            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 14; color: "#dce2ec" }
            Item { Layout.fillWidth: true }
            AppText { text: "本次流量"; color: "#7483a5"; font.pixelSize: 11 }
            AppText { text: (speed.dataMiB * 1.048576).toFixed(0) + " MB"; color: "#080e36"; font.pixelSize: 12 }
            Item { Layout.fillWidth: true }
            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 14; color: "#dce2ec" }
            Item { Layout.fillWidth: true }
            AppText { text: speed.testedAt || "—"; color: "#526589"; font.pixelSize: 12 }
        }
        AppText { visible: speed.error.length > 0; text: speed.error; color: "#d14b58"; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
    }
}
