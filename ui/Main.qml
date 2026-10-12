import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    function t(source) { return i18n.language === "en" ? i18n.translate(String(source)) : String(source) }
    id: window
    width: 960; height: 620
    minimumWidth: 960; minimumHeight: 620
    visible: true
    title: client.demo ? t("OpenLnku · 离线演示") : "OpenLnku"
    color: "#f3f5f8"
    property int page: 0
    readonly property string networkContext: String(speed.mode) + ":" + String(speed.protocolChoice) + ":" + String(client.selectedAdapter) + ":" + String(client.interfaceIndex()) + ":" + client.ip
    onPageChanged: { if (page === 1) infoRefresh.restart() }
    onNetworkContextChanged: { networkInfo.invalidate(); if (page === 1) infoRefresh.restart() }
    Timer {
        id: infoRefresh; interval: 250
        onTriggered: {
            if (window.page !== 1 || speed.running) return
            if (networkInfo.refreshing || networkInfo.checkingDns) { restart(); return }
            networkInfo.refresh(speed.mode,speed.protocolChoice,client.interfaceIndex(),client.ip)
        }
    }
    Connections { target: speed; function onChanged() { if (window.page === 1 && !speed.running) infoRefresh.restart() } }
    function previewNetworkDetails() { networkInfo.detectDns(speed.mode,speed.protocolChoice,client.interfaceIndex(),client.ip); networkPage.contentItem.contentY = 500 }
    function previewSpeedOptions() { speedPanel.previewOptions() }

    property bool forceClose: false
    property bool revealPassword: false
    readonly property color ink: "#080e36"
    readonly property color muted: "#738096"
    font.family: i18n.language === "en" ? "Segoe UI" : "Microsoft YaHei UI"
    font.pixelSize: 14
    onClosing: function(close) {
        if (!forceClose) {
            close.accepted = false
            client.closeWindow()
        }
    }
    Shortcut { sequence: "Ctrl+1"; onActivated: window.page = 0 }
    Shortcut { sequence: "Ctrl+2"; onActivated: window.page = 1 }
    Shortcut { sequence: "Ctrl+3"; onActivated: window.page = 2 }
    Shortcut { sequence: "Ctrl+4"; onActivated: window.page = 3 }
    Shortcut { sequence: "Ctrl+L"; onActivated: { window.page = 0; account.forceActiveFocus() } }
    Shortcut { sequence: "Escape"; onActivated: { if (client.closeToTray) client.closeWindow() } }

    Rectangle {
        id: sidebar
        width: 194
        anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
        anchors.margins: 10
        radius: 22; color: "white"
        Column {
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 18; spacing: 9
            Row {
                height: 66; spacing: 9
                Image { width: 40; height: 40; anchors.verticalCenter: parent.verticalCenter; source: "qrc:/branding/app.png"; sourceSize.width: 120; sourceSize.height: 120 }
                AppText { text: "OpenLnku"; font.family: "Segoe UI"; font.pixelSize: 20; font.bold: true; anchors.verticalCenter: parent.verticalCenter }
            }
            Item { width: 1; height: 8 }
            Repeater {
                model: [t("概览"), t("网络"), t("日志"), t("设置")]
                delegate: Button {
                    id: nav
                    required property int index
                    required property string modelData
                    width: 158; height: 53
                    onClicked: window.page = index
                    background: Rectangle { radius: 14; color: window.page === nav.index ? "#edf1ff" : nav.hovered ? "#f5f7fb" : "transparent" }
                    contentItem: Row {
                        spacing: 15; leftPadding: 15
                        AppIcon { kind: ["home","network","log","settings"][nav.index]; ink: window.page === nav.index ? "#184cff" : window.ink; width: 23; height: 23; anchors.verticalCenter: parent.verticalCenter }
                        AppText { text: nav.modelData; color: window.page === nav.index ? "#184cff" : window.ink; font.pixelSize: 16; font.weight: window.page === nav.index ? Font.Bold : Font.Normal; anchors.verticalCenter: parent.verticalCenter }
                    }
                    Accessible.name: modelData
                }
            }
        }
        AppText { anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 22; text: "OpenLnku · v" + Qt.application.version; color: "#8793a7"; font.pixelSize: 12 }
    }

    Item {
        anchors.left: sidebar.right; anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
        anchors.leftMargin: 24; anchors.rightMargin: 24; anchors.topMargin: 26; anchors.bottomMargin: 20
        RowLayout {
            id: pageHeading
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 42
            AppText { objectName: "pageTitle"; text: [t("连接概览"),t("网络"),t("日志"),t("设置")][window.page]; font.pixelSize: 29; font.bold: true }
            Rectangle {
                visible: client.demo; Layout.leftMargin: 8; width: 80; height: 27; radius: 14; color: "#e0e8ff"
                AppText { anchors.centerIn: parent; text: t("离线演示"); color: "#4057d6"; font.pixelSize: 12 }
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                visible: window.page === 1
                Layout.preferredWidth: 102; Layout.preferredHeight: 34; radius: 17; color: client.online ? "#e5f5ed" : "#edf1f7"
                Row { anchors.centerIn: parent; spacing: 8; AppIcon { kind: "check"; ink: "#008d55"; width: 19; height: 19; visible: client.online } AppText { text: client.demo ? t("离线演示") : client.online ? t("认证正常") : t("未连接"); color: client.online ? "#008d55" : window.muted; font.pixelSize: 12; font.weight: Font.DemiBold } }
            }
        }
        StackLayout {
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: pageHeading.bottom; anchors.topMargin: 18; anchors.bottom: parent.bottom
            currentIndex: window.page
            ScrollView {
                id: overview
                Layout.minimumHeight: 0; Layout.minimumWidth: 0
                clip: true; contentWidth: availableWidth
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ColumnLayout {
                    width: overview.availableWidth; spacing: 16
                    Rectangle {
                        Layout.fillWidth: true; Layout.preferredHeight: 150
                        radius: 20; color: "#e1eeff"
                        RowLayout {
                            anchors.fill: parent; anchors.margins: 26; spacing: 20
                            Rectangle {
                                Layout.preferredWidth: 60; Layout.preferredHeight: 60; radius: 30; color: "#f5f9ff"
                                AppIcon { anchors.centerIn: parent; width: 32; height: 32; kind: client.online ? "check" : "power"; ink: "#245ac0" }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 9
                                AppText { text: t(client.status); color: "#1f4fad"; font.pixelSize: 29; font.bold: true }
                                AppText { text: t(client.detail); color: "#4877b0"; font.pixelSize: 14; wrapMode: Text.WordWrap; Layout.fillWidth: true; maximumLineCount: 3; elide: Text.ElideRight }
                            }
                            ColumnLayout {
                                spacing: 8; Layout.alignment: Qt.AlignVCenter
                                ActionButton {
                                    text: client.active ? t("断开连接") : client.busy ? t("正在断开") : client.demo ? t("模拟连接") : t("连接")
                                    enabled: !client.busy || client.active
                                    implicitWidth: 146; implicitHeight: 49
                                    onClicked: client.active ? client.disconnectNow() : client.connectNow(account.text, secret.text, remember.checked)
                                }
                                ActionButton { visible: client.retrySeconds > 0; text: t("立即重试 · ") + client.retrySeconds + "s"; primary: false; implicitWidth: 146; onClicked: client.retryNow() }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 12
                        Repeater {
                            model: [
                                {label:t("认证状态"), value:client.online ? client.demo ? t("模拟通过") : t("已通过") : client.active ? t("连接中") : t("未连接"), icon:"shield"},
                                {label:t("互联网访问"), value:t(client.internetStatus), icon:client.internetOutcome === "success" ? "check" : client.internetOutcome === "failure" ? "cross" : "globe"},
                                {label:t("在线时长"), value:client.uptime, icon:"clock"},
                                {label:t("最近心跳"), value:t(client.heartbeatAge), icon:"heartbeat"}
                            ]
                            delegate: Panel {
                                required property var modelData
                                required property int index
                                Layout.fillWidth: true; Layout.preferredWidth: 1; Layout.preferredHeight: 110
                                padding: 16
                                ColumnLayout {
                                    anchors.fill: parent; spacing: 7
                                    RowLayout {
                                        Rectangle {
                                            width: 27; height: 27; radius: 9
                                            color: index === 1 && client.internetOutcome === "success" ? "#e7f7ef" : index === 1 && client.internetOutcome === "failure" ? "#fff0f0" : "#f0f4fb"
                                            AppIcon { anchors.centerIn: parent; kind: modelData.icon; ink: index === 1 && client.internetOutcome === "success" ? "#209365" : index === 1 && client.internetOutcome === "failure" ? "#d14b58" : index === 0 ? "#4057d6" : "#536681"; width: 19; height: 19 }
                                        }
                                        AppText { text: t(modelData.label); color: window.muted; font.pixelSize: 12; Layout.fillWidth: true }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        AppText { text: t(modelData.value); color: index === 1 && client.internetOutcome === "success" ? "#209365" : index === 1 && client.internetOutcome === "failure" ? "#c24653" : index === 0 ? "#4057d6" : window.ink; font.pixelSize: index === 1 ? 17 : 20; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true }
                                        Button {
                                            visible: index === 1; enabled: !client.checkingInternet
                                            implicitWidth: 27; implicitHeight: 27
                                            onClicked: client.checkInternet()
                                            background: Rectangle { color: parent.hovered ? "#e2ebff" : "#f0f4ff"; radius: 8 }
                                            contentItem: AppIcon { kind: "refresh"; ink: "#245ac0" }
                                            Accessible.name: t("检测互联网访问")
                                            ToolTip.visible: hovered; ToolTip.text: t("检测互联网访问")
                                        }
                                    }
                                    AppText { visible: index === 1; text: "HTTPS · " + client.internetLatency; color: window.muted; font.pixelSize: 11; font.family: "Segoe UI" }
                                }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 16
                        Panel {
                            Layout.fillWidth: true; Layout.preferredWidth: 1; Layout.preferredHeight: 224; padding: 24
                            ColumnLayout {
                                anchors.fill: parent; spacing: 13
                                RowLayout { AppIcon { kind: "user" } AppText { text: t("认证账号"); font.pixelSize: 18; font.weight: Font.DemiBold } }
                                RowLayout {
                                    Layout.fillWidth: true
                                    AppText { text: t("用户名"); color: window.muted; Layout.preferredWidth: 64 }
                                    Field { id: account; Layout.fillWidth: true; text: client.savedUser; placeholderText: t("输入账号"); enabled: !client.busy; readOnly: client.demo; maximumLength: 36; Accessible.name: t("用户名") }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    AppText { text: t("密码"); color: window.muted; Layout.preferredWidth: 64 }
                                    Field {
                                        id: secret; Layout.fillWidth: true; text: client.savedPassword; placeholderText: t("输入密码"); rightPadding: 40
                                        echoMode: window.revealPassword ? TextInput.Normal : TextInput.Password
                                        enabled: !client.busy; readOnly: client.demo; maximumLength: 16
                                        onAccepted: client.connectNow(account.text, secret.text, remember.checked)
                                        Accessible.name: t("密码")
                                        Button {
                                            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; anchors.rightMargin: 6
                                            width: 30; height: 30; onClicked: window.revealPassword = !window.revealPassword
                                            background: Rectangle { radius: 8; color: parent.hovered ? "#edf2fa" : "transparent" }
                                            contentItem: AppIcon { kind: "eye"; ink: window.revealPassword ? "#245ac0" : "#738096" }
                                            Accessible.name: window.revealPassword ? t("隐藏密码") : t("显示密码")
                                        }
                                    }
                                }
                                CheckBox {
                                    id: remember; text: t("记住密码"); checked: client.remembered; enabled: !client.demo && !client.busy
                                    Layout.leftMargin: 76; implicitHeight: 29
                                    indicator: Rectangle { width: 18; height: 18; radius: 5; y: 5; color: remember.checked ? "#4057d6" : "white"; border.color: remember.checked ? "#4057d6" : "#cbd3e0"; AppIcon { anchors.fill: parent; anchors.margins: 2; kind: "check"; ink: "white"; visible: remember.checked } }
                                    contentItem: AppText { text: remember.text; leftPadding: 26; verticalAlignment: Text.AlignVCenter; color: "#526078" }
                                }
                            }
                        }
                        Panel {
                            Layout.fillWidth: true; Layout.preferredWidth: 1; Layout.preferredHeight: 224; padding: 24
                            ColumnLayout {
                                anchors.fill: parent; spacing: 3
                                RowLayout {
                                    Layout.bottomMargin: 8
                                    AppIcon { kind: "traffic"; ink: "#326bdc" }
                                    AppText { text: t("流量吞吐"); font.pixelSize: 18; font.weight: Font.DemiBold }
                                    Item { Layout.fillWidth: true }
                                    AppText { text: client.demo ? t("演示未采集") : t("实体网卡"); font.pixelSize: 11; color: window.muted }
                                }
                                RowLayout {
                                    Layout.fillWidth: true; spacing: 18
                                    ColumnLayout {
                                        Layout.fillWidth: true; spacing: 3
                                        RowLayout { AppIcon { kind: "download"; width: 15; height: 15; ink: "#326bdc" } AppText { text: t("下载"); font.pixelSize: 11; color: "#738096" } }
                                        AppText { text: client.downloadRate; Layout.fillWidth: true; font.family: "Segoe UI"; font.pixelSize: 22; minimumPixelSize: 15; fontSizeMode: Text.Fit; font.weight: Font.DemiBold; color: "#245ac0" }
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true; spacing: 3
                                        RowLayout { AppIcon { kind: "upload"; width: 15; height: 15; ink: "#32999f" } AppText { text: t("上传"); font.pixelSize: 11; color: "#738096" } }
                                        AppText { text: client.uploadRate; Layout.fillWidth: true; font.family: "Segoe UI"; font.pixelSize: 22; minimumPixelSize: 15; fontSizeMode: Text.Fit; font.weight: Font.DemiBold; color: "#32999f" }
                                    }
                                }
                                Item {
                                    Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 55
                                    TrafficChart { anchors.fill: parent; samples: client.trafficHistory }
                                    AppText { visible: !client.trafficAvailable; anchors.centerIn: parent; text: client.demo ? t("离线演示 · 无流量采集") : t("等待网卡数据"); font.pixelSize: 11; color: window.muted }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    AppText { text: t("本次累计 ") + client.trafficTotal; font.pixelSize: 11; color: "#526078" }
                                    Item { Layout.fillWidth: true }
                                    AppText { text: "↓ " + client.downloadTotal + "   ↑ " + client.uploadTotal; font.family: "Segoe UI"; font.pixelSize: 11; color: window.muted }
                                }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 16
                        Panel {
                            Layout.fillWidth: true; Layout.preferredWidth: 1; Layout.preferredHeight: 224; padding: 24
                            ColumnLayout {
                                anchors.fill: parent; spacing: 3
                                RowLayout { Layout.bottomMargin: 10; AppIcon { kind: "path" } AppText { text: t("连接路径"); font.pixelSize: 18; font.weight: Font.DemiBold } }
                                InfoRow { Layout.fillWidth: true; label: t("接入网卡"); value: client.adapterLabel }
                                InfoRow { Layout.fillWidth: true; label: t("认证绑定"); value: t(client.bindingLabel) }
                                InfoRow { Layout.fillWidth: true; label: t("系统目标路由"); value: t(client.routeName) }
                                InfoRow { Layout.fillWidth: true; label: t("认证服务器"); value: "10.100.61.3 : 61440" }
                            }
                        }
                        Panel {
                            Layout.fillWidth: true; Layout.preferredWidth: 1; Layout.preferredHeight: 224; padding: 24
                            ColumnLayout {
                                anchors.fill: parent; spacing: 9
                                RowLayout { AppIcon { kind: "log" } AppText { text: t("最近活动"); font.pixelSize: 18; font.weight: Font.DemiBold } Item { Layout.fillWidth: true } Button { text: t("全部"); flat: true; onClicked: window.page = 2; implicitHeight: 30 } }
                                Repeater {
                                    model: client.recentLogs
                                    AppText { required property string modelData; text: t(modelData); font.pixelSize: 12; color: "#68788f"; Layout.fillWidth: true; elide: Text.ElideRight }
                                }
                                Item { Layout.fillHeight: true }
                            }
                        }
                    }
                }
            }
            ScrollView {
                id: networkPage; clip: true; contentWidth: availableWidth; ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                Layout.minimumHeight: 0; Layout.minimumWidth: 0
                ColumnLayout {
                    width: networkPage.availableWidth; spacing: 10
                    Panel {
                        Layout.fillWidth: true; Layout.preferredHeight: 66; padding: 20
                        RowLayout {
                            anchors.fill: parent; spacing: 12
                            AppIcon { kind: "path"; ink: "#184cff"; width: 28; height: 28 }
                            AppText { text: t("接入网卡"); font.pixelSize: 15; font.weight: Font.DemiBold }
                            SelectField { Layout.preferredWidth: 190; model: client.adapterItems; currentIndex: client.selectedAdapter; displayText: currentIndex < 0 ? t("选择接入网卡") : t(client.adapterLabel); implicitHeight: 38; onActivated: function(index) { client.selectedAdapter = index } }
                            Item { Layout.fillWidth: true }
                            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 37; color: "#e5e8f0" }
                            AppText { text: t("本地地址"); font.pixelSize: 12; color: window.muted; Layout.leftMargin: 14 }
                            AppText { text: client.ip; font.family: "Segoe UI"; font.pixelSize: 14; Layout.fillWidth: true }
                        }
                    }
                    SpeedPanel { id: speedPanel; Layout.fillWidth: true }
                    ExitInfoPanel { Layout.fillWidth: true }
                    DnsPanel { Layout.fillWidth: true }
                    Panel {
                        Layout.fillWidth: true; Layout.preferredHeight: 253
                        ColumnLayout {
                            anchors.fill: parent; spacing: 5
                            RowLayout { Layout.bottomMargin: 8; AppIcon { kind: "path" } AppText { text: t("认证通道"); font.pixelSize: 18; font.weight: Font.DemiBold } }
                            InfoRow { Layout.fillWidth: true; label: t("认证服务器"); value: "10.100.61.3 : 61440 / UDP" }
                            InfoRow { Layout.fillWidth: true; label: t("认证绑定"); value: t(client.bindingLabel) }
                            InfoRow { Layout.fillWidth: true; label: t("系统目标路由"); value: t(client.routeName) }
                            InfoRow { Layout.fillWidth: true; label: t("下一跳"); value: t(client.routeLabel) }
                            InfoRow { Layout.fillWidth: true; label: t("有效心跳"); value: String(client.heartbeats) }
                        }
                    }
                    Panel {
                        Layout.fillWidth: true; Layout.preferredHeight: 141
                        ColumnLayout {
                            anchors.fill: parent; spacing: 16
                            RowLayout { AppIcon { kind: "shield" } AppText { text: t("网络检查"); font.pixelSize: 18; font.weight: Font.DemiBold } Item { Layout.fillWidth: true } AppIcon { kind: client.internetOutcome === "success" ? "check" : client.internetOutcome === "failure" ? "cross" : "globe"; ink: client.internetOutcome === "success" ? "#209365" : client.internetOutcome === "failure" ? "#d14b58" : "#738096" } AppText { text: t(client.internetStatus); color: client.internetOutcome === "failure" ? "#d14b58" : "#245ac0" } AppText { text: "HTTPS · " + client.internetLatency; color: window.muted; font.pixelSize: 12 } }
                            RowLayout {
                                spacing: 12
                                ActionButton { text: t("检查认证通道"); primary: false; onClicked: { client.diagnose(); window.page = 2 } }
                                ActionButton { text: t("刷新网卡"); primary: false; onClicked: client.refreshNetwork() }
                                ActionButton { text: client.checkingInternet ? t("检测中") : t("检测互联网"); primary: false; enabled: !client.checkingInternet; onClicked: client.checkInternet() }
                                Item { Layout.fillWidth: true }
                            }
                        }
                    }
                    Panel {
                        visible: client.demo; Layout.fillWidth: true; Layout.preferredHeight: 138
                        ColumnLayout {
                            anchors.fill: parent; spacing: 14
                            AppText { text: t("离线测试"); font.pixelSize: 18; font.weight: Font.DemiBold }
                            RowLayout { ActionButton { text: t("模拟丢包"); primary: false; enabled: client.active; onClicked: client.simulateDrop() } ActionButton { text: t("模拟唤醒"); primary: false; onClicked: client.simulateResume() } Item { Layout.fillWidth: true } }
                        }
                    }
                }
            }
            ColumnLayout {
                spacing: 16
                RowLayout {
                    Layout.fillWidth: true
                    SelectField { id: logFilter; model: [t("全部记录"), t("异常记录")]; implicitWidth: 134; implicitHeight: 40 }
                    Item { Layout.fillWidth: true }
                    ActionButton { text: t("复制"); primary: false; onClicked: client.copyLogs() }
                    ActionButton { text: t("导出"); primary: false; onClicked: client.exportLogs(t("导出运行日志")) }
                    ActionButton { text: t("清空"); primary: false; onClicked: client.clearLogs() }
                }
                Panel {
                    Layout.fillWidth: true; Layout.fillHeight: true; padding: 20
                    ListView {
                        id: logList; anchors.fill: parent; clip: true
                        model: logFilter.currentIndex === 0 ? client.logs : client.logs.filter(function(line) { return /失败|错误|超时|异常|拒绝|不可用/.test(line) })
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Item {
                            required property string modelData
                            width: logList.width; height: logText.implicitHeight + 28
                            AppText { id: logText; anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.topMargin: 12; text: t(modelData); font.pixelSize: 13; color: /失败|错误|超时|异常|拒绝/.test(modelData) ? "#ac5d39" : "#526078"; wrapMode: Text.WordWrap }
                            Rectangle { width: parent.width; height: 1; anchors.bottom: parent.bottom; color: "#edf0f5" }
                        }
                        AppText { visible: logList.count === 0; anchors.centerIn: parent; text: t("暂无记录"); color: window.muted }
                    }
                }
            }
            ScrollView {
                id: settingsPage; clip: true; contentWidth: availableWidth; ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ColumnLayout {
                    width: settingsPage.availableWidth; spacing: 16
                    Panel {
                        Layout.fillWidth: true; Layout.preferredHeight: 96; padding: 22
                        RowLayout {
                            anchors.fill: parent; spacing: 14
                            AppIcon { kind: "globe"; ink: "#184cff" }
                            AppText { text: t("界面语言"); font.pixelSize: 18; font.weight: Font.DemiBold }
                            Item { Layout.fillWidth: true }
                            SelectField {
                                objectName: "languageSelector"
                                Layout.preferredWidth: 200; implicitHeight: 40
                                model: ["简体中文", "English"]
                                currentIndex: i18n.language === "en" ? 1 : 0
                                onActivated: function(index) { i18n.language = index === 1 ? "en" : "zh-CN" }
                                Accessible.name: t("界面语言")
                            }
                        }
                    }
                    Panel {
                        Layout.fillWidth: true; Layout.preferredHeight: updates.available ? 246 : 156; padding: 22
                        ColumnLayout {
                            anchors.fill: parent; spacing: 12
                            RowLayout { AppIcon { kind: "refresh"; ink: "#326bff" } AppText { text: t("版本与更新"); font.pixelSize: 18; font.weight: Font.DemiBold } Item { Layout.fillWidth: true } AppText { text: "v" + Qt.application.version; color: window.muted } }
                            RowLayout { Layout.fillWidth: true; AppText { text: t(updates.status); Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#526078"; font.pixelSize: 12 } AppText { visible: updates.busy && updates.progress > 0; text: (updates.progress * 100).toFixed(0) + "%"; color: "#245ac0" } }
                            ProgressBar { visible: updates.busy && updates.progress > 0; Layout.fillWidth: true; value: updates.progress; implicitHeight: 5 }
                            AppText { visible: updates.available; text: t(updates.notes); textFormat: Text.PlainText; maximumLineCount: 3; elide: Text.ElideRight; wrapMode: Text.WordWrap; Layout.fillWidth: true; color: window.muted; font.pixelSize: 11 }
                            RowLayout {
                                ActionButton { text: updates.recoveryAvailable ? t("恢复上次更新") : updates.busy ? t("取消") : t("检查更新"); primary: false; enabled: !updates.installing && !speed.running && (!updates.recoveryAvailable || !updates.busy); onClicked: updates.recoveryAvailable ? updates.recover() : updates.busy ? updates.cancel() : updates.check() }
                                ActionButton { visible: updates.available; enabled: !updates.busy && !speed.running; text: updates.ready ? t("重启并更新") : t("下载更新"); onClicked: updates.ready ? updates.install() : updates.download() }
                                Item { Layout.fillWidth: true }
                            }
                        }
                    }
                    Panel {
                        Layout.fillWidth: true; Layout.preferredHeight: 259
                        ColumnLayout {
                            anchors.fill: parent; spacing: 3
                            RowLayout { Layout.bottomMargin: 10; AppIcon { kind: "settings" } AppText { text: t("连接与启动"); font.pixelSize: 18; font.weight: Font.DemiBold } }
                            PreferenceRow { Layout.fillWidth: true; label: t("开机自启动"); checked: client.startWithWindows; enabled: !client.demo; onToggled: function(value) { client.startWithWindows = value } }
                            Rectangle { Layout.fillWidth: true; height: 1; color: "#edf0f5" }
                            PreferenceRow { Layout.fillWidth: true; label: t("启动时自动连接"); checked: client.autoLogin; enabled: !client.demo; onToggled: function(value) { client.autoLogin = value } }
                            Rectangle { Layout.fillWidth: true; height: 1; color: "#edf0f5" }
                            PreferenceRow { Layout.fillWidth: true; label: t("自动重连"); checked: client.autoReconnect; onToggled: function(value) { client.autoReconnect = value } }
                            Rectangle { Layout.fillWidth: true; height: 1; color: "#edf0f5" }
                            PreferenceRow { Layout.fillWidth: true; label: t("关闭窗口后保留托盘"); checked: client.closeToTray; enabled: trayAvailable; onToggled: function(value) { client.closeToTray = value } }
                        }
                    }
                    Panel {
                        Layout.fillWidth: true; Layout.preferredHeight: 229
                        ColumnLayout {
                            anchors.fill: parent; spacing: 5
                            RowLayout { Layout.bottomMargin: 8; AppIcon { kind: "globe" } AppText { text: t("通知与检测"); font.pixelSize: 18; font.weight: Font.DemiBold } }
                            PreferenceRow { Layout.fillWidth: true; label: t("持续断线与恢复通知"); checked: client.notifications; onToggled: function(value) { client.notifications = value } }
                            Rectangle { Layout.fillWidth: true; height: 1; color: "#edf0f5" }
                            PreferenceRow { Layout.fillWidth: true; label: t("每 5 分钟检测互联网"); checked: client.periodicCheck; enabled: !client.demo; onToggled: function(value) { client.periodicCheck = value } }
                            RowLayout {
                                Layout.fillWidth: true; spacing: 14
                                AppText { text: t("检测地址"); color: window.muted }
                                Field { id: probeAddress; Layout.fillWidth: true; text: client.probeUrl; Accessible.name: t("检测地址"); onAccepted: client.probeUrl = text }
                                ActionButton { text: t("保存"); primary: false; onClicked: { client.probeUrl = probeAddress.text; probeAddress.text = client.probeUrl } }
                            }
                        }
                    }
                    Panel {
                        Layout.fillWidth: true; Layout.preferredHeight: 178
                        ColumnLayout {
                            anchors.fill: parent; spacing: 16
                            RowLayout { AppText { text: "OpenLnku"; font.pixelSize: 19; font.bold: true } Item { Layout.fillWidth: true } AppText { text: "v" + Qt.application.version; color: window.muted } }
                            RowLayout {
                                ActionButton { text: t("清除账号"); primary: false; enabled: !client.busy; onClicked: clearAccountDialog.open() }
                                ActionButton { text: t("开源许可"); primary: false; onClicked: client.openLicense() }
                                ActionButton { text: t("退出"); primary: false; onClicked: client.requestQuit() }
                                Item { Layout.fillWidth: true }
                            }
                            AppText { text: "© 2026 OpenLnku"; font.pixelSize: 12; color: window.muted }
                        }
                    }
                }
            }
        }
    }
    Dialog {
        id: licenseDialog
        objectName: "licenseDialog"
        anchors.centerIn: parent; modal: true
        width: Math.min(window.width - 70, 790); height: Math.min(window.height - 80, 650)
        title: t("开源许可"); padding: 22
        background: Rectangle { color: "white"; radius: 20; border.color: "#e4eaf3" }
        header: Item {
            implicitHeight: 60
            AppText { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; anchors.leftMargin: 24; text: t("开源许可"); font.pixelSize: 21; font.weight: Font.DemiBold }
        }
        footer: Item {
            implicitHeight: 68
            ActionButton { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; anchors.rightMargin: 22; text: t("关闭"); implicitWidth: 108; onClicked: licenseDialog.close() }
        }
        contentItem: ScrollView {
            clip: true
            TextArea {
                id: licenseBody
                readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                textFormat: TextEdit.PlainText; font.family: "Segoe UI"; font.pixelSize: 13
                color: window.ink; background: null
            }
        }
    }
    Dialog {
        id: errorDialog
        anchors.centerIn: parent; modal: true; width: Math.min(window.width - 70, 520)
        property string message: ""
        title: t("操作未完成")
        contentItem: AppText { text: t(errorDialog.message); wrapMode: Text.WordWrap }
        footer: ActionButton { text: t("关闭"); onClicked: errorDialog.close() }
    }
    Connections {
        target: client
        function onLicenseRequested() { licenseBody.text = client.licenseText(); licenseDialog.open() }
        function onErrorRequested(message) { errorDialog.message = message; errorDialog.open() }
    }
    Dialog {
        id: clearAccountDialog
        anchors.centerIn: parent; modal: true
        title: t("清除保存的账号与密码？")
        footer: RowLayout {
            ActionButton { text: t("取消"); primary: false; onClicked: clearAccountDialog.close() }
            ActionButton { text: t("清除账号"); onClicked: { client.clearCredentials(); clearAccountDialog.close() } }
        }
    }
}
