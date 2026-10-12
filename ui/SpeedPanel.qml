import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Panel {
    function t(source) { return i18n.language === "en" ? i18n.translate(String(source)) : String(source) }
    id: card
    padding: 18
    implicitHeight: speed.error.length ? 464 : 438
    function previewOptions() { optionsPopup.open() }
    function number(value) { return value < 0 ? "—" : value.toFixed(0) }
    ColumnLayout {
        anchors.fill: parent; spacing: 7
        RowLayout {
            Layout.fillWidth: true; Layout.preferredHeight: 40
            AppIcon { kind: "speed"; ink: "#184cff"; Layout.preferredWidth: 32; Layout.preferredHeight: 32 }
            AppText { text: t("网络测速"); color: "#080e36"; font.pixelSize: 21; font.weight: Font.Bold; Layout.leftMargin: 6 }
            Item { Layout.fillWidth: true }
            ActionButton { text: speed.running ? t("取消测速") : t("开始测速"); accent: "#1b59d7"; enabled: speed.running || (!networkInfo.refreshing && !networkInfo.checkingDns); implicitWidth: 122; implicitHeight: 41; onClicked: { optionsPopup.close(); speed.running ? speed.cancel() : speed.start(client.interfaceIndex(), client.ip, client.adapterLabel) } }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 12; Layout.preferredHeight: 33
            Rectangle {
                Layout.preferredWidth: 280; Layout.preferredHeight: 33; radius: 9; color: "white"; border.color: "#d5ddec"
                RowLayout {
                    anchors.fill: parent; anchors.margins: 2; spacing: 2
                    Repeater {
                        model: [t("系统路由"), t("实体网卡直连")]
                        Button {
                            required property int index; required property string modelData
                            Layout.fillWidth: true; Layout.fillHeight: true; enabled: !speed.running
                            onClicked: speed.mode = index
                            background: Rectangle { radius: 8; color: speed.mode === parent.index ? "#e9eeff" : "transparent" }
                            contentItem: AppText { text: t(parent.modelData); color: speed.mode === parent.index ? "#184cff" : "#6e7d9d"; font.pixelSize: 12; font.weight: speed.mode === parent.index ? Font.Bold : Font.Normal; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        }
                    }
                }
            }
            AppText {
                text: [speed.protocol || [t("自动协议"), "IPv4", "IPv6"][speed.protocolChoice], speed.extended ? t("延长测速") : t("标准测速")].join(" · ")
                color: "#7483a5"; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight
            }
            Button {
                id: optionsButton
                Layout.preferredWidth: 116; Layout.preferredHeight: 33
                enabled: !speed.running; hoverEnabled: true
                onClicked: optionsPopup.visible ? optionsPopup.close() : optionsPopup.open()
                Accessible.name: t("测速选项")
                background: Rectangle { radius: 9; color: optionsPopup.visible || optionsButton.hovered ? "#e9eeff" : "#f5f8ff"; border.color: "#e2e7f2" }
                contentItem: Row {
                    spacing: 7
                    AppIcon { kind: "settings"; ink: optionsButton.enabled ? "#526589" : "#a2a9b7"; width: 16; height: 16; anchors.verticalCenter: parent.verticalCenter }
                    AppText { text: t("测速选项"); font.pixelSize: 12; color: optionsButton.enabled ? "#526589" : "#a2a9b7"; anchors.verticalCenter: parent.verticalCenter }
                }
                Popup {
                    id: optionsPopup
                    x: optionsButton.width - width; y: optionsButton.height + 8
                    width: 330; padding: 18
                    implicitHeight: optionsContents.implicitHeight + padding * 2
                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                    background: Rectangle { radius: 14; color: "white"; border.color: "#dce3f0" }
                    contentItem: ColumnLayout {
                        id: optionsContents
                        spacing: 14
                        AppText { text: t("测速选项"); font.pixelSize: 16; font.weight: Font.DemiBold }
                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#edf0f6" }
                        GridLayout {
                            Layout.fillWidth: true; columns: 2; rowSpacing: 12; columnSpacing: 16
                            AppText { text: t("测速节点"); font.pixelSize: 12; color: "#7483a5" }
                            SelectField {
                                Layout.fillWidth: true; implicitHeight: 36; fill: "#f7f9ff"
                                model: [t("自动选择"), t("国内节点"), t("海外节点")]
                                currentIndex: speed.nodeChoice; enabled: !speed.running
                                onActivated: speed.nodeChoice = currentIndex; Accessible.name: t("测速节点")
                            }
                            AppText { text: t("IP 协议"); font.pixelSize: 12; color: "#7483a5" }
                            SelectField {
                                Layout.fillWidth: true; implicitHeight: 36; fill: "#f7f9ff"
                                model: [t("自动协议"), "IPv4", "IPv6"]
                                currentIndex: speed.protocolChoice; enabled: !speed.running
                                onActivated: speed.protocolChoice = currentIndex; Accessible.name: t("IP 协议")
                            }
                            AppText { text: t("测速时长"); font.pixelSize: 12; color: "#7483a5" }
                            SelectField {
                                Layout.fillWidth: true; implicitHeight: 36; fill: "#f7f9ff"
                                model: [t("标准测速"), t("延长测速")]
                                currentIndex: speed.extended ? 1 : 0; enabled: !speed.running
                                onActivated: speed.extended = currentIndex === 1; Accessible.name: t("测速时长")
                            }
                            AppText { text: t("下载 / 上传"); font.pixelSize: 12; color: "#7483a5" }
                            AppText { text: speed.extended ? "20 s / 15 s" : "10 s / 7 s"; font.pixelSize: 12; color: "#526589" }
                        }
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.preferredHeight: 171; Layout.minimumHeight: 171; Layout.maximumHeight: 171; spacing: 14
            SpeedGauge { Layout.preferredWidth: 304; Layout.fillHeight: true; value: speed.uploading ? speed.upload : speed.download; uploading: speed.uploading; phase: speed.phase; complete: speed.complete }
            GridLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; columns: 2; columnSpacing: 8; rowSpacing: 8
                Repeater {
                    model: [
                        {label:t("下载"), icon:"downloadArrow", value:speed.download > 0 ? speed.download.toFixed(1) : "—", unit:"Mbps", tint:"#245bd4", bg:"#eaf3ff"},
                        {label:t("上传"), icon:"uploadArrow", value:speed.upload > 0 ? speed.upload.toFixed(1) : "—", unit:"Mbps", tint:"#008d55", bg:"#e5f5ed"},
                        {label:t("HTTP 延迟"), icon:"clock", value:card.number(speed.latency), unit:"ms", tint:"#080e36", bg:"#f0f3ff"},
                        {label:t("抖动"), icon:"heartbeat", value:card.number(speed.jitter), unit:"ms", tint:"#080e36", bg:"#f0f3ff"}
                    ]
                    Rectangle {
                        required property var modelData
                        Layout.fillWidth: true; Layout.fillHeight: true; radius: 9; color: "white"; border.color: "#edf0f7"
                        RowLayout {
                            anchors.fill: parent; anchors.margins: 12; spacing: 12
                            Rectangle { Layout.preferredWidth: 43; Layout.preferredHeight: 43; radius: 22; color: modelData.bg; AppIcon { anchors.centerIn: parent; kind: modelData.icon; ink: modelData.tint; width: 25; height: 25 } }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 1
                                AppText { text: t(modelData.label); font.pixelSize: 12; color: "#7483a5" }
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
                    Row { spacing: 6; Rectangle { width: 7; height: 7; radius: 4; color: "#3475ff"; anchors.verticalCenter: parent.verticalCenter } AppText { text: t("下载"); color: "#7483a5"; font.pixelSize: 11 } }
                    Row { spacing: 6; Rectangle { width: 7; height: 7; radius: 4; color: "#009d98"; anchors.verticalCenter: parent.verticalCenter } AppText { text: t("上传"); color: "#7483a5"; font.pixelSize: 11 } }
                    Item { Layout.fillWidth: true }
                }
                SpeedChart { Layout.fillWidth: true; Layout.fillHeight: true; samples: speed.history }
            }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#edf0f6" }
        RowLayout {
            Layout.fillWidth: true; Layout.preferredHeight: 21; spacing: 13
            AppText { text: t("节点"); color: "#7483a5"; font.pixelSize: 11 }
            AppText { text: t(speed.node); color: "#080e36"; font.pixelSize: 12 }
            Item { Layout.fillWidth: true }
            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 14; color: "#dce2ec" }
            Item { Layout.fillWidth: true }
            AppText { text: t("本次流量"); color: "#7483a5"; font.pixelSize: 11 }
            AppText { text: (speed.dataMiB * 1.048576).toFixed(0) + " MB"; color: "#080e36"; font.pixelSize: 12 }
            Item { Layout.fillWidth: true }
            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 14; color: "#dce2ec" }
            Item { Layout.fillWidth: true }
            AppText { text: speed.testedAt || "—"; color: "#526589"; font.pixelSize: 12 }
        }
        AppText { visible: speed.error.length > 0; text: t(speed.error); color: "#d14b58"; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
    }
}
