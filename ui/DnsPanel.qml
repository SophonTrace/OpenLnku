import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Panel {
    id: panel
    function t(source) { return i18n.language === "en" ? i18n.translate(String(source)) : String(source) }
    objectName: "dnsPanel"
    padding: 20
    implicitHeight: dnsContents.implicitHeight + padding * 2
    ColumnLayout {
        id: dnsContents
        anchors.fill: parent; spacing: 12
        RowLayout {
            Layout.fillWidth: true
            Rectangle { Layout.preferredWidth: 32; Layout.preferredHeight: 32; radius: 10; color: "#edf2ff"; AppIcon { anchors.centerIn: parent; kind: "dns"; ink: "#184cff"; width: 22; height: 22 } }
            AppText { text: t("DNS 泄露检测"); font.pixelSize: 18; font.weight: Font.DemiBold; Layout.leftMargin: 5 }
            Item { Layout.fillWidth: true }
            ActionButton { text: networkInfo.checkingDns ? t("取消检测") : t("开始检测"); implicitWidth: 105; implicitHeight: 35; enabled: !speed.running && !networkInfo.refreshing; onClicked: networkInfo.checkingDns ? networkInfo.cancel() : networkInfo.detectDns(speed.mode,speed.protocolChoice,client.interfaceIndex(),client.ip) }
        }
        RowLayout {
            Layout.fillWidth: true
            AppText { text: t(networkInfo.dnsState); color: networkInfo.dnsState.indexOf("核查") >= 0 ? "#b87924" : "#536d94"; font.pixelSize: 12; font.weight: Font.DemiBold }
            Item { Layout.fillWidth: true }
            AppText { text: t(networkInfo.dnsScope || (speed.mode ? "所选网卡 DNS" : "系统 DNS")); font.pixelSize: 11; color: "#7483a5" }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#edf0f5" }
        Repeater {
            model: networkInfo.dns
            Rectangle {
                required property var modelData
                Layout.fillWidth: true; Layout.preferredHeight: 50; radius: 10; color: "#f6f9ff"
                RowLayout {
                    anchors.fill: parent; anchors.margins: 10; spacing: 10
                    Image { source: modelData.countryCode ? "qrc:/flags/" + modelData.countryCode.toLowerCase() + ".svg" : ""; sourceSize.width: 64; Layout.preferredWidth: 28; Layout.preferredHeight: 21; fillMode: Image.PreserveAspectFit; visible: status === Image.Ready }
                    ColumnLayout { Layout.fillWidth: true; spacing: 2; AppText { text: modelData.ip; font.family: "Segoe UI"; font.pixelSize: 13; Layout.fillWidth: true; elide: Text.ElideMiddle } AppText { text: [modelData.country || "",modelData.position || ""].filter(Boolean).join(" · ") || t("地区未知"); font.pixelSize: 10; color: "#7483a5"; Layout.fillWidth: true; elide: Text.ElideRight } }
                    ColumnLayout { Layout.maximumWidth: 220; spacing: 2; AppText { text: modelData.provider || t("服务商未知"); font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight } AppText { text: (modelData.asn || "—") + (modelData.probe ? " · " + t(modelData.probe) : ""); font.pixelSize: 10; color: "#7483a5" } }
                }
            }
        }
        AppText { visible: networkInfo.dns.length === 0; text: networkInfo.checkingDns ? t("正在观测随机域名的 DNS 出口") : t("尚无 DNS 出口记录"); font.pixelSize: 12; color: "#8b98ae"; Layout.preferredHeight: 45; verticalAlignment: Text.AlignVCenter }
    }
}
