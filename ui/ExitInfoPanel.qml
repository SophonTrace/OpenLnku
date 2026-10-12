import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Panel {
    id: panel
    function t(source) { return i18n.language === "en" ? i18n.translate(String(source)) : String(source) }
    function value(key) { return networkInfo.info[key] || "—" }
    property bool hiddenIp: false
    padding: 20
    implicitHeight: networkInfo.error.length ? 226 : 202
    objectName: "exitInfoPanel"
    ColumnLayout {
        anchors.fill: parent; spacing: 13
        RowLayout {
            Layout.fillWidth: true
            Rectangle { Layout.preferredWidth: 32; Layout.preferredHeight: 32; radius: 10; color: "#eaf3ff"; AppIcon { anchors.centerIn: parent; kind: "location"; ink: "#184cff"; width: 22; height: 22 } }
            AppText { text: t("出口信息"); font.pixelSize: 18; font.weight: Font.DemiBold; Layout.leftMargin: 5 }
            Item { Layout.fillWidth: true }
            AppText { text: networkInfo.refreshing ? t("检测中") : value("protocol"); font.pixelSize: 11; color: "#7483a5" }
            Button {
                implicitWidth: 30; implicitHeight: 30; enabled: !speed.running && !networkInfo.checkingDns
                onClicked: networkInfo.refreshing ? networkInfo.cancel() : networkInfo.refresh(speed.mode,speed.protocolChoice,client.interfaceIndex(),client.ip,true)
                Accessible.name: t("刷新出口信息")
                background: Rectangle { radius: 8; color: parent.hovered ? "#edf3ff" : "transparent" }
                contentItem: AppIcon { kind: networkInfo.refreshing ? "cross" : "refresh"; ink: "#184cff" }
            }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#edf0f5" }
        RowLayout {
            Layout.fillWidth: true; spacing: 15
            ColumnLayout {
                Layout.preferredWidth: 218; spacing: 9
                RowLayout {
                    Image { source: value("countryCode") !== "—" ? "qrc:/flags/" + value("countryCode").toLowerCase() + ".svg" : ""; sourceSize.width: 96; Layout.preferredWidth: 44; Layout.preferredHeight: 33; fillMode: Image.PreserveAspectFit; visible: status === Image.Ready }
                    AppText { text: t(value("country")) !== "—" ? t(value("country")) : value("countryCode"); font.pixelSize: 17; font.weight: Font.DemiBold; Layout.fillWidth: true; elide: Text.ElideRight }
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 6
                    AppText { text: t("出口 IP"); font.pixelSize: 11; color: "#7483a5" }
                    AppText { text: panel.hiddenIp ? "••••••••" : value("ip"); font.pixelSize: 12; font.family: "Segoe UI"; Layout.fillWidth: true; elide: Text.ElideMiddle; HoverHandler { id: ipHover } ToolTip.visible: ipHover.hovered && !panel.hiddenIp; ToolTip.text: value("ip") }
                    Button { implicitWidth: 24; implicitHeight: 24; padding: 2; Accessible.name: t("隐藏出口 IP"); onClicked: panel.hiddenIp = !panel.hiddenIp; background: Item {} contentItem: AppIcon { width: 20; height: 20; kind: panel.hiddenIp ? "eyeOff" : "eye"; ink: "#7483a5" } }
                    Button { implicitWidth: 24; implicitHeight: 24; padding: 2; Accessible.name: t("复制出口 IP"); enabled: value("ip") !== "—"; onClicked: networkInfo.copyIp(); background: Item {} contentItem: AppIcon { width: 20; height: 20; kind: "copy"; ink: "#7483a5" } }
                }
                AppText { text: t(value("route")); color: "#7483a5"; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
            }
            Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: "#e5e9f1" }
            GridLayout {
                Layout.fillWidth: true; columns: 2; rowSpacing: 8; columnSpacing: 12
                Repeater {
                    model: [t("服务商"),value("provider"),t("位置"),value("position"),t("时区"),value("timezone"),t("自治域"),value("asn")]
                    AppText { required property int index; required property string modelData; text: modelData; font.pixelSize: 12; color: index % 2 ? "#080e36" : "#7483a5"; Layout.fillWidth: index % 2 === 1; elide: Text.ElideRight; HoverHandler { id: valueHover } ToolTip.visible: valueHover.hovered; ToolTip.text: modelData }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            AppText { text: "Cloudflare · IPLeak"; font.pixelSize: 10; color: "#8b98ae"; HoverHandler { id: sourceHover } ToolTip.visible: sourceHover.hovered; ToolTip.text: t("IP 为 Cloudflare 查询出口；地区为该 IP 的近似归属地，代理分流可能产生不同出口") }
            Item { Layout.fillWidth: true }
            AppText { text: value("coordinates"); font.pixelSize: 10; color: "#8b98ae" }
        }
        AppText { visible: networkInfo.error.length > 0; text: t(networkInfo.error); font.pixelSize: 11; color: "#a26b28"; Layout.fillWidth: true; wrapMode: Text.WordWrap }
    }
}
