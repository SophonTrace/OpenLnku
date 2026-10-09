import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    width: 960; height: 620
    minimumWidth: 960; minimumHeight: 620
    visible: true
    title: client.demo ? "OpenNex · 离线演示" : "OpenNex"
    color: "#f3f5f8"
    property int page: 0
    property bool forceClose: false
    property bool revealPassword: false
    readonly property color ink: "#080e36"
    readonly property color muted: "#738096"
    font.family: "Microsoft YaHei UI"
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
                AppText { text: "OpenNex"; font.family: "Segoe UI"; font.pixelSize: 20; font.bold: true; anchors.verticalCenter: parent.verticalCenter }
            }
            Item { width: 1; height: 8 }
            Repeater {
                model: ["概览", "网络", "日志", "设置"]
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
        AppText { anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 22; text: "OpenNex · v" + Qt.application.version; color: "#8793a7"; font.pixelSize: 12 }
    }

    Item {
        anchors.left: sidebar.right; anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
        anchors.leftMargin: 24; anchors.rightMargin: 24; anchors.topMargin: 26; anchors.bottomMargin: 20
        RowLayout {
            id: pageHeading
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 42
            AppText { text: ["连接概览","网络","日志","设置"][window.page]; font.pixelSize: 29; font.bold: true }
            Rectangle {
                visible: client.demo; Layout.leftMargin: 8; width: 80; height: 27; radius: 14; color: "#e0e8ff"
                AppText { anchors.centerIn: parent; text: "离线演示"; color: "#4057d6"; font.pixelSize: 12 }
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                visible: window.page === 1
                Layout.preferredWidth: 102; Layout.preferredHeight: 34; radius: 17; color: client.online ? "#e5f5ed" : "#edf1f7"
                Row { anchors.centerIn: parent; spacing: 8; AppIcon { kind: "check"; ink: "#008d55"; width: 19; height: 19; visible: client.online } AppText { text: client.demo ? "离线演示" : client.online ? "认证正常" : "未连接"; color: client.online ? "#008d55" : window.muted; font.pixelSize: 12; font.weight: Font.DemiBold } }
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
                                AppText { text: client.status; color: "#1f4fad"; font.pixelSize: 29; font.bold: true }
                                AppText { text: client.detail; color: "#4877b0"; font.pixelSize: 14; wrapMode: Text.WordWrap; Layout.fillWidth: true; maximumLineCount: 3; elide: Text.ElideRight }
                            }
                            ColumnLayout {
                                spacing: 8; Layout.alignment: Qt.AlignVCenter
                                ActionButton {
                                    text: client.active ? "断开连接" : client.busy ? "正在断开" : client.demo ? "模拟连接" : "连接"
                                    enabled: !client.busy || client.active
                                    implicitWidth: 146; implicitHeight: 49
                                    onClicked: client.active ? client.disconnectNow() : client.connectNow(account.text, secret.text, remember.checked)
                                }
                                ActionButton { visible: client.retrySeconds > 0; text: "立即重试 · " + client.retrySeconds + "s"; primary: false; implicitWidth: 146; onClicked: client.retryNow() }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 12
                        Repeater {
                            model: [
                                {label:"认证状态", value:client.online ? client.demo ? "模拟通过" : "已通过" : client.active ? "连接中" : "未连接", icon:"shield"},
                                {label:"互联网访问", value:client.internetStatus, icon:client.internetOutcome === "success" ? "check" : client.internetOutcome === "failure" ? "cross" : "globe"},
                                {label:"在线时长", value:client.uptime, icon:"clock"},
                                {label:"最近心跳", value:client.heartbeatAge, icon:"heartbeat"}
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
                                        AppText { text: modelData.label; color: window.muted; font.pixelSize: 12; Layout.fillWidth: true }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        AppText { text: modelData.value; color: index === 1 && client.internetOutcome === "success" ? "#209365" : index === 1 && client.internetOutcome === "failure" ? "#c24653" : index === 0 ? "#4057d6" : window.ink; font.pixelSize: index === 1 ? 17 : 20; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true }
                                        Button {
                                            visible: index === 1; enabled: !client.checkingInternet
                                            implicitWidth: 27; implicitHeight: 27
                                            onClicked: client.checkInternet()
                                            background: Rectangle { color: parent.hovered ? "#e2ebff" : "#f0f4ff"; radius: 8 }
                                            contentItem: AppIcon { kind: "refresh"; ink: "#245ac0" }
                                            Accessible.name: "检测互联网访问"
                                            ToolTip.visible: hovered; ToolTip.text: "检测互联网访问"
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
                                RowLayout { AppIcon { kind: "user" } AppText { text: "认证账号"; font.pixelSize: 18; font.weight: Font.DemiBold } }
                                RowLayout {
                                    Layout.fillWidth: true
                                    AppText { text: "用户名"; color: window.muted; Layout.preferredWidth: 64 }
                                    Field { id: account; Layout.fillWidth: true; text: client.savedUser; placeholderText: "输入账号"; enabled: !client.busy; readOnly: client.demo; maximumLength: 36; Accessible.name: "用户名" }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    AppText { text: "密码"; color: window.muted; Layout.preferredWidth: 64 }
                                    Field {
                                        id: secret; Layout.fillWidth: true; text: client.savedPassword; placeholderText: "输入密码"; rightPadding: 40
                                        echoMode: window.revealPassword ? TextInput.Normal : TextInput.Password
                                        enabled: !client.busy; readOnly: client.demo; maximumLength: 16
                                        onAccepted: client.connectNow(account.text, secret.text, remember.checked)
                                        Accessible.name: "密码"
                                        Button {
                                            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; anchors.rightMargin: 6
                                            width: 30; height: 30; onClicked: window.revealPassword = !window.revealPassword
                                            background: Rectangle { radius: 8; color: parent.hovered ? "#edf2fa" : "transparent" }
                                            contentItem: AppIcon { kind: "eye"; ink: window.revealPassword ? "#245ac0" : "#738096" }
                                            Accessible.name: window.revealPassword ? "隐藏密码" : "显示密码"
                                        }
                                    }
                                }
                                CheckBox {
                                    id: remember; text: "记住密码"; checked: client.remembered; enabled: !client.demo && !client.busy
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
                                    AppText { text: "流量吞吐"; font.pixelSize: 18; font.weight: Font.DemiBold }
                                    Item { Layout.fillWidth: true }
                                    AppText { text: client.demo ? "演示未采集" : "实体网卡"; font.pixelSize: 11; color: window.muted }
                                }
                                RowLayout {
                                    Layout.fillWidth: true; spacing: 18
                                    ColumnLayout {
                                        Layout.fillWidth: true; spacing: 3
                                        RowLayout { AppIcon { kind: "download"; width: 15; height: 15; ink: "#326bdc" } AppText { text: "下载"; font.pixelSize: 11; color: "#738096" } }
                                        AppText { text: client.downloadRate; Layout.fillWidth: true; font.family: "Segoe UI"; font.pixelSize: 22; minimumPixelSize: 15; fontSizeMode: Text.Fit; font.weight: Font.DemiBold; color: "#245ac0" }
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true; spacing: 3
                                        RowLayout { AppIcon { kind: "upload"; width: 15; height: 15; ink: "#32999f" } AppText { text: "上传"; font.pixelSize: 11; color: "#738096" } }
                                        AppText { text: client.uploadRate; Layout.fillWidth: true; font.family: "Segoe UI"; font.pixelSize: 22; minimumPixelSize: 15; fontSizeMode: Text.Fit; font.weight: Font.DemiBold; color: "#32999f" }
                                    }
                                }
                                Item {
                                    Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 55
                                    TrafficChart { anchors.fill: parent; samples: client.trafficHistory }
                                    AppText { visible: !client.trafficAvailable; anchors.centerIn: parent; text: client.demo ? "离线演示 · 无流量采集" : "等待网卡数据"; font.pixelSize: 11; color: window.muted }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    AppText { text: "本次累计 " + client.trafficTotal; font.pixelSize: 11; color: "#526078" }
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
                                RowLayout { Layout.bottomMargin: 10; AppIcon { kind: "path" } AppText { text: "连接路径"; font.pixelSize: 18; font.weight: Font.DemiBold } }
                                InfoRow { Layout.fillWidth: true; label: "接入网卡"; value: client.adapterLabel }
                                InfoRow { Layout.fillWidth: true; label: "认证绑定"; value: client.bindingLabel }
                                InfoRow { Layout.fillWidth: true; label: "系统目标路由"; value: client.routeName }
                                InfoRow { Layout.fillWidth: true; label: "认证服务器"; value: "10.100.61.3 : 61440" }
                            }
                        }
                        Panel {
                            Layout.fillWidth: true; Layout.preferredWidth: 1; Layout.preferredHeight: 224; padding: 24
                            ColumnLayout {
                                anchors.fill: parent; spacing: 9
                                RowLayout { AppIcon { kind: "log" } AppText { text: "最近活动"; font.pixelSize: 18; font.weight: Font.DemiBold } Item { Layout.fillWidth: true } Button { text: "全部"; flat: true; onClicked: window.page = 2; implicitHeight: 30 } }
                                Repeater {
                                    model: client.recentLogs
                                    AppText { required property string modelData; text: modelData; font.pixelSize: 12; color: "#68788f"; Layout.fillWidth: true; elide: Text.ElideRight }
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
                            AppText { text: "接入网卡"; font.pixelSize: 15; font.weight: Font.DemiBold }
                            SelectField { Layout.preferredWidth: 190; model: client.adapterItems; currentIndex: client.selectedAdapter; displayText: currentIndex < 0 ? "选择接入网卡" : client.adapterLabel; implicitHeight: 38; onActivated: function(index) { client.selectedAdapter = index } }
                            Item { Layout.fillWidth: true }
                            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 37; color: "#e5e8f0" }
                            AppText { text: "本地地址"; font.pixelSize: 12; color: window.muted; Layout.leftMargin: 14 }
                            AppText { text: client.ip; font.family: "Segoe UI"; font.pixelSize: 14; Layout.fillWidth: true }
                        }
                    }
                    SpeedPanel { Layout.fillWidth: true }
                    Panel {
                        Layout.fillWidth: true; Layout.preferredHeight: 253
                        ColumnLayout {
                            anchors.fill: parent; spacing: 5
                            RowLayout { Layout.bottomMargin: 8; AppIcon { kind: "path" } AppText { text: "认证通道"; font.pixelSize: 18; font.weight: Font.DemiBold } }
                            InfoRow { Layout.fillWidth: true; label: "认证服务器"; value: "10.100.61.3 : 61440 / UDP" }
                            InfoRow { Layout.fillWidth: true; label: "认证绑定"; value: client.bindingLabel }
                            InfoRow { Layout.fillWidth: true; label: "系统目标路由"; value: client.routeName }
                            InfoRow { Layout.fillWidth: true; label: "下一跳"; value: client.routeLabel }
                            InfoRow { Layout.fillWidth: true; label: "有效心跳"; value: String(client.heartbeats) }
                        }
                    }
                    Panel {
                        Layout.fillWidth: true; Layout.preferredHeight: 141
                        ColumnLayout {
                            anchors.fill: parent; spacing: 16
                            RowLayout { AppIcon { kind: "shield" } AppText { text: "网络检查"; font.pixelSize: 18; font.weight: Font.DemiBold } Item { Layout.fillWidth: true } AppIcon { kind: client.internetOutcome === "success" ? "check" : client.internetOutcome === "failure" ? "cross" : "globe"; ink: client.internetOutcome === "success" ? "#209365" : client.internetOutcome === "failure" ? "#d14b58" : "#738096" } AppText { text: client.internetStatus; color: client.internetOutcome === "failure" ? "#d14b58" : "#245ac0" } AppText { text: "HTTPS · " + client.internetLatency; color: window.muted; font.pixelSize: 12 } }
                            RowLayout {
                                spacing: 12
                                ActionButton { text: "检查认证通道"; primary: false; onClicked: { client.diagnose(); window.page = 2 } }
                                ActionButton { text: "刷新网卡"; primary: false; onClicked: client.refreshNetwork() }
                                ActionButton { text: client.checkingInternet ? "检测中" : "检测互联网"; primary: false; enabled: !client.checkingInternet; onClicked: client.checkInternet() }
                                Item { Layout.fillWidth: true }
                            }
                        }
                    }
                    Panel {
                        visible: client.demo; Layout.fillWidth: true; Layout.preferredHeight: 138
                        ColumnLayout {
                            anchors.fill: parent; spacing: 14
                            AppText { text: "离线测试"; font.pixelSize: 18; font.weight: Font.DemiBold }
                            RowLayout { ActionButton { text: "模拟丢包"; primary: false; enabled: client.active; onClicked: client.simulateDrop() } ActionButton { text: "模拟唤醒"; primary: false; onClicked: client.simulateResume() } Item { Layout.fillWidth: true } }
                        }
                    }
                }
            }
            ColumnLayout {
                spacing: 16
                RowLayout {
                    Layout.fillWidth: true
                    SelectField { id: logFilter; model: ["全部记录", "异常记录"]; implicitWidth: 134; implicitHeight: 40 }
                    Item { Layout.fillWidth: true }
                    ActionButton { text: "复制"; primary: false; onClicked: client.copyLogs() }
                    ActionButton { text: "导出"; primary: false; onClicked: client.exportLogs() }
                    ActionButton { text: "清空"; primary: false; onClicked: client.clearLogs() }
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
                            AppText { id: logText; anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.topMargin: 12; text: modelData; font.pixelSize: 13; color: /失败|错误|超时|异常|拒绝/.test(modelData) ? "#ac5d39" : "#526078"; wrapMode: Text.WordWrap }
                            Rectangle { width: parent.width; height: 1; anchors.bottom: parent.bottom; color: "#edf0f5" }
                        }
                        AppText { visible: logList.count === 0; anchors.centerIn: parent; text: "暂无记录"; color: window.muted }
                    }
                }
            }
            ScrollView {
                id: settingsPage; clip: true; contentWidth: availableWidth; ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ColumnLayout {
                    width: settingsPage.availableWidth; spacing: 16
                    Panel {
                        Layout.fillWidth: true; Layout.preferredHeight: updates.available ? 246 : 156; padding: 22
                        ColumnLayout {
                            anchors.fill: parent; spacing: 12
                            RowLayout { AppIcon { kind: "refresh"; ink: "#326bff" } AppText { text: "版本与更新"; font.pixelSize: 18; font.weight: Font.DemiBold } Item { Layout.fillWidth: true } AppText { text: "v" + Qt.application.version; color: window.muted } }
                            RowLayout { Layout.fillWidth: true; AppText { text: updates.status; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#526078"; font.pixelSize: 12 } AppText { visible: updates.busy && updates.progress > 0; text: (updates.progress * 100).toFixed(0) + "%"; color: "#245ac0" } }
                            ProgressBar { visible: updates.busy && updates.progress > 0; Layout.fillWidth: true; value: updates.progress; implicitHeight: 5 }
                            AppText { visible: updates.available; text: updates.notes; textFormat: Text.PlainText; maximumLineCount: 3; elide: Text.ElideRight; wrapMode: Text.WordWrap; Layout.fillWidth: true; color: window.muted; font.pixelSize: 11 }
                            RowLayout {
                                ActionButton { text: updates.recoveryAvailable ? "恢复上次更新" : updates.busy ? "取消" : "检查更新"; primary: false; enabled: !speed.running; onClicked: updates.recoveryAvailable ? updates.recover() : updates.busy ? updates.cancel() : updates.check() }
                                ActionButton { visible: updates.available; enabled: !updates.busy && !speed.running; text: updates.ready ? "重启并更新" : "下载更新"; onClicked: updates.ready ? updates.install() : updates.download() }
                                Item { Layout.fillWidth: true }
                            }
                        }
                    }
                    Panel {
                        Layout.fillWidth: true; Layout.preferredHeight: 259
                        ColumnLayout {
                            anchors.fill: parent; spacing: 3
                            RowLayout { Layout.bottomMargin: 10; AppIcon { kind: "settings" } AppText { text: "连接与启动"; font.pixelSize: 18; font.weight: Font.DemiBold } }
                            PreferenceRow { Layout.fillWidth: true; label: "开机自启动"; checked: client.startWithWindows; enabled: !client.demo; onToggled: function(value) { client.startWithWindows = value } }
                            Rectangle { Layout.fillWidth: true; height: 1; color: "#edf0f5" }
                            PreferenceRow { Layout.fillWidth: true; label: "启动时自动连接"; checked: client.autoLogin; enabled: !client.demo; onToggled: function(value) { client.autoLogin = value } }
                            Rectangle { Layout.fillWidth: true; height: 1; color: "#edf0f5" }
                            PreferenceRow { Layout.fillWidth: true; label: "自动重连"; checked: client.autoReconnect; onToggled: function(value) { client.autoReconnect = value } }
                            Rectangle { Layout.fillWidth: true; height: 1; color: "#edf0f5" }
                            PreferenceRow { Layout.fillWidth: true; label: "关闭窗口后保留托盘"; checked: client.closeToTray; enabled: trayAvailable; onToggled: function(value) { client.closeToTray = value } }
                        }
                    }
                    Panel {
                        Layout.fillWidth: true; Layout.preferredHeight: 229
                        ColumnLayout {
                            anchors.fill: parent; spacing: 5
                            RowLayout { Layout.bottomMargin: 8; AppIcon { kind: "globe" } AppText { text: "通知与检测"; font.pixelSize: 18; font.weight: Font.DemiBold } }
                            PreferenceRow { Layout.fillWidth: true; label: "持续断线与恢复通知"; checked: client.notifications; onToggled: function(value) { client.notifications = value } }
                            Rectangle { Layout.fillWidth: true; height: 1; color: "#edf0f5" }
                            PreferenceRow { Layout.fillWidth: true; label: "每 5 分钟检测互联网"; checked: client.periodicCheck; enabled: !client.demo; onToggled: function(value) { client.periodicCheck = value } }
                            RowLayout {
                                Layout.fillWidth: true; spacing: 14
                                AppText { text: "检测地址"; color: window.muted }
                                Field { id: probeAddress; Layout.fillWidth: true; text: client.probeUrl; Accessible.name: "检测地址"; onAccepted: client.probeUrl = text }
                                ActionButton { text: "保存"; primary: false; onClicked: { client.probeUrl = probeAddress.text; probeAddress.text = client.probeUrl } }
                            }
                        }
                    }
                    Panel {
                        Layout.fillWidth: true; Layout.preferredHeight: 178
                        ColumnLayout {
                            anchors.fill: parent; spacing: 16
                            RowLayout { AppText { text: "OpenNex"; font.pixelSize: 19; font.bold: true } Item { Layout.fillWidth: true } AppText { text: "v" + Qt.application.version; color: window.muted } }
                            RowLayout {
                                ActionButton { text: "清除账号"; primary: false; enabled: !client.busy; onClicked: clearAccountDialog.open() }
                                ActionButton { text: "开源许可"; primary: false; onClicked: client.openLicense() }
                                ActionButton { text: "退出"; primary: false; onClicked: client.requestQuit() }
                                Item { Layout.fillWidth: true }
                            }
                            AppText { text: "© 2026 OpenNex"; font.pixelSize: 12; color: window.muted }
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
        title: "开源许可"; padding: 22
        background: Rectangle { color: "white"; radius: 20; border.color: "#e4eaf3" }
        header: Item {
            implicitHeight: 60
            AppText { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; anchors.leftMargin: 24; text: "开源许可"; font.pixelSize: 21; font.weight: Font.DemiBold }
        }
        footer: Item {
            implicitHeight: 68
            ActionButton { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; anchors.rightMargin: 22; text: "关闭"; implicitWidth: 108; onClicked: licenseDialog.close() }
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
        title: "操作未完成"; standardButtons: Dialog.Ok
        contentItem: AppText { id: errorBody; wrapMode: Text.WordWrap }
    }
    Connections {
        target: client
        function onLicenseRequested() { licenseBody.text = client.licenseText(); licenseDialog.open() }
        function onErrorRequested(message) { errorBody.text = message; errorDialog.open() }
    }
    Dialog {
        id: clearAccountDialog
        anchors.centerIn: parent; modal: true
        title: "清除保存的账号与密码？"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: client.clearCredentials()
    }
}
