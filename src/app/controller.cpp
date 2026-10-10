#include "controller.h"
#include "settings_migration.h"
#include <windows.h>
#include <wincrypt.h>
#include <QApplication>
#include <QDateTime>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QClipboard>
#include <QNetworkProxyFactory>
#include <QNetworkRequest>
#include <QUrl>
#include <algorithm>

namespace {
QByteArray protect(QByteArray bytes, bool encrypt) {
    DATA_BLOB input{static_cast<DWORD>(bytes.size()), reinterpret_cast<BYTE*>(bytes.data())}, output{};
    const BOOL ok = encrypt ? CryptProtectData(&input, L"OpenLnku credential", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)
                            : CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output);
    if (!ok) { bytes.fill('\0'); return {}; }
    QByteArray result(reinterpret_cast<const char*>(output.pbData), static_cast<qsizetype>(output.cbData));
    SecureZeroMemory(output.pbData, output.cbData); LocalFree(output.pbData); bytes.fill('\0'); return result;
}
QString reason(const std::string& message) {
    if (message == "account or password rejected") return QStringLiteral("账号或密码被拒绝，请检查后重新连接。");
    if (message == "MAC rejected; check selected adapter") return QStringLiteral("网卡 MAC 未通过认证，请检查接入网卡。");
    if (message.starts_with("discarded response: ")) return QStringLiteral("收到不符合协议的响应：%1").arg(QString::fromStdString(message.substr(20)));
    if (message.find("logout timed out") != std::string::npos) return QStringLiteral("注销未获确认，自动重连已暂停。");
    if (message.find("logout confirmed") != std::string::npos) return QStringLiteral("注销已确认，自动重连已暂停。");
    if (message.find("timeout") != std::string::npos) return QStringLiteral("响应超时，稍后建立新会话重新认证。");
    if (message.find("transport") != std::string::npos) return QStringLiteral("认证通道暂不可用，正在等待重试。");
    if (message.find("server rejected") != std::string::npos) return QStringLiteral("服务器暂未接受登录，稍后重试。");
    return {};
}
QString bytesText(double value) {
    const QStringList units{"B", "KiB", "MiB", "GiB", "TiB"};
    int unit = 0;
    while (value >= 1024.0 && unit < units.size() - 1) { value /= 1024.0; ++unit; }
    return QStringLiteral("%1 %2").arg(value, 0, 'f', unit ? 1 : 0).arg(units[unit]);
}
}
Controller::Controller(bool demo, QObject* parent, const QString& settingsFile) : QObject(parent), demo_(demo) {
    clock_.start();
    if (!settingsFile.isEmpty()) settings_ = std::make_unique<QSettings>(settingsFile, QSettings::IniFormat);
    else if (!demo_) {
        settings_ = std::make_unique<QSettings>("OpenLnku", "OpenLnku");
        QSettings previous("OpenNex", "OpenNex");
        migrateOpenNexPreferences(*settings_, previous);
        QSettings legacy("JluConnect", "JluConnect");
        migratePreferences(*settings_, legacy);
    }
    if (settings_) {
        autoLogin_ = settings_->value("autoLogin", true).toBool();
        autoReconnect_ = settings_->value("autoReconnect", true).toBool();
        closeToTray_ = settings_->value("closeToTray", true).toBool();
        notifications_ = settings_->value("notifications", true).toBool();
        periodicCheck_ = settings_->value("periodicCheck", false).toBool();
        savedAdapterId_ = settings_->value("adapterId").toString();
        const auto url = settings_->value("probeUrl", probeUrl_).toString();
        if (QUrl(url).isValid() && QUrl(url).scheme() == "https" && !QUrl(url).host().isEmpty() && QUrl(url).userInfo().isEmpty()) probeUrl_ = url;
    }
    session_.setAutoReconnect(autoReconnect_);
    if (!demo_ && (!settings_->value("startupConfigured", false).toBool() || !settings_->value("brandStartupMigrated", false).toBool()))
        setStartWithWindows(settings_->value("startWithWindows", true).toBool());
    if (demo_) { savedUser_ = "example001"; savedPassword_ = "demo1234"; }
    else {
        transport_ = std::make_unique<jlu::UdpTransport>();
        savedUser_ = settings_->value("username").toString();
        const auto blob = settings_->value("credential").toByteArray();
        savedPassword_ = QString::fromUtf8(protect(QByteArray::fromBase64(blob), false));
        remembered_ = !savedPassword_.isEmpty();
    }
    refreshNetwork();
    qApp->installNativeEventFilter(this);
    log(demo_ ? QStringLiteral("离线演示：不验证账号密码，不连接网络。") : QStringLiteral("认证服务已就绪。"));
    connect(&timer_, &QTimer::timeout, this, &Controller::pump); timer_.start(100);
    if (!demo_ && autoLogin() && remembered_) QTimer::singleShot(700, this, [this] { connectNow(savedUser_, savedPassword_, true); });
}
Controller::~Controller() { qApp->removeNativeEventFilter(this); }
std::uint64_t Controller::now() const { return static_cast<std::uint64_t>(clock_.elapsed()); }
QString Controller::status() const {
    if (online()) return demo_ ? QStringLiteral("模拟流程通过") : QStringLiteral("认证已通过");
    using enum jlu::State;
    switch (session_.state()) {
    case Online: return demo_ ? QStringLiteral("模拟流程通过") : QStringLiteral("认证已通过");
    case Idle: return QStringLiteral("尚未连接");
    case Paused: return QStringLiteral("已断开");
    case Error: return QStringLiteral("认证失败");
    case Suspended: return QStringLiteral("等待唤醒");
    case Backoff: return QStringLiteral("正在重连");
    case WaitingNetwork: return QStringLiteral("等待网络");
    case LogoutChallenge: case Logout: return QStringLiteral("正在断开");
    case Challenge: return QStringLiteral("正在握手");
    case Login: return QStringLiteral("正在认证");
    case Keep38: case Extra: case Keep1: case Keep2: return QStringLiteral("正在确认心跳");
    default: return QStringLiteral("正在认证");
    }
}
QString Controller::detail() const {
    if (!localError_.isEmpty()) return localError_;
    if (transport_ && session_.message().find("transport") != std::string::npos) {
        const auto& error = transport_->error();
        if (error.starts_with("UDP 61440 unavailable")) return QStringLiteral("无法占用 UDP 61440。请完全退出占用认证端口的其他客户端后重试。");
        if (!error.empty()) return QStringLiteral("认证通道错误：%1").arg(QString::fromStdString(error));
    }
    const auto specific = reason(session_.message()); if (!specific.isEmpty()) return specific;
    if (online()) return demo_ ? QStringLiteral("仅完成本地模拟，未验证账号密码，也未连接网络。")
                              : QStringLiteral("服务器已确认认证与心跳");
    using enum jlu::State;
    switch (session_.state()) {
    case Idle: return demo_ ? QStringLiteral("体验连接流程，不影响当前网络与代理。") : QStringLiteral("选择接入网卡，使用认证账号连接。");
    case Online: return demo_ ? QStringLiteral("仅完成本地模拟，未验证账号密码，也未连接网络。")
                             : QStringLiteral("服务器已确认认证与心跳；互联网访问还取决于网络及代理状态。");
    case Paused: return QStringLiteral("自动重连已暂停，点击连接可重新开启。");
    case WaitingNetwork: return QStringLiteral("等待接入网卡和地址稳定，再建立新的认证会话。");
    case Suspended: return QStringLiteral("旧会话已释放，唤醒后将重新检查网络。");
    case Backoff: return QStringLiteral("等待重试，下一次将重新握手与认证。");
    case Challenge: return QStringLiteral("正在与认证服务器握手。");
    case Login: return QStringLiteral("正在提交认证信息。");
    case Keep38: case Extra: case Keep1: case Keep2: return QStringLiteral("正在验证心跳与会话，确认后显示在线。");
    case LogoutChallenge: case Logout: return QStringLiteral("正在请求注销，自动重连已暂停。");
    default: return QStringLiteral("请检查账号和网络后重新连接。");
    }
}
std::optional<jlu::Host> Controller::selectedHost() const {
    if (selected_ >= 0 && static_cast<std::size_t>(selected_) < adapters_.size()) return adapters_[static_cast<std::size_t>(selected_)].host;
    if (demo_) { jlu::Host h; h.ip = {192, 0, 2, 44}; h.mac = {2, 0, 0, 0, 0, 1}; return h; }
    return {};
}
QString Controller::adapterLabel() const {
    if (static_cast<std::size_t>(selected_) < adapters_.size()) return QString::fromStdString(adapters_[static_cast<std::size_t>(selected_)].name);
    return demo_ ? QStringLiteral("模拟接入网卡") : QStringLiteral("未找到可用接入网卡");
}
QString Controller::ip() const { auto h = selectedHost(); return h ? QString::fromStdString(jlu::ipText(h->ip)) : QStringLiteral("—"); }
bool Controller::routeConflict() const { auto h = selectedHost(); return route_.available && h && route_.interfaceIndex != h->interfaceIndex; }
QString Controller::routeLabel() const {
    if (!route_.available) return QStringLiteral("暂未找到到认证服务器的路由");
    return QStringLiteral("接口 %1 · 下一跳 %2").arg(route_.interfaceIndex).arg(QString::fromStdString(route_.nextHop));
}
QString Controller::routeName() const {
    if (!route_.available) return QStringLiteral("未检测到路由");
    return route_.interfaceName.empty() ? QStringLiteral("接口 %1").arg(route_.interfaceIndex) : QString::fromStdString(route_.interfaceName);
}
QString Controller::bindingLabel() const {
    if (demo_) return QStringLiteral("离线模拟");
    if (!transport_ || !transport_->isOpen()) return QStringLiteral("未绑定");
    const auto host = session_.host();
    return host ? QStringLiteral("物理接口 %1").arg(host->interfaceIndex) : QStringLiteral("未绑定");
}
QString Controller::heartbeatAge() const {
    return online() && lastHeartbeat_ ? QStringLiteral("%1 秒前").arg((now() - lastHeartbeat_) / 1000) : QStringLiteral("—");
}
int Controller::retrySeconds() const {
    if (session_.state() != jlu::State::Backoff) return 0;
    return static_cast<int>((session_.retryAt() > now() ? session_.retryAt() - now() + 999 : 0) / 1000);
}
QVariantList Controller::adapterItems() const {
    QVariantList result;
    for (const auto& a : adapters_) result.push_back(QString::fromStdString(a.name + " · " + jlu::ipText(a.host.ip)));
    if (result.isEmpty() && demo_) result.push_back(QStringLiteral("模拟接入网卡 · 192.0.2.44"));
    return result;
}
void Controller::setSelectedAdapter(int index) {
    if (index < 0 || index >= adapterItems().size()) return;
    selected_ = index;
    if (!demo_ && static_cast<std::size_t>(index) < adapters_.size()) {
        savedAdapterId_ = QString::fromStdString(adapters_[static_cast<std::size_t>(index)].id);
        savePreference("adapterId", savedAdapterId_);
    }
    session_.network(selectedHost(), now()); synchronizeTransport(); emit changed();
}
void Controller::refreshNetwork() {
    if (demo_) {
        jlu::Host host; host.ip = {192, 168, 1, 108}; host.mac = {2, 0, 0, 0, 0, 1}; host.interfaceIndex = 18;
        adapters_ = {{host, "以太网 2", "Offline preview", "preview-ethernet"}};
        selected_ = 0; route_ = {99, "192.0.2.1", true, "Mihomo TUN"};
        session_.network(selectedHost(), now()); lastNetwork_ = now(); emit changed(); return;
    }
    const auto previousId = selected_ >= 0 && static_cast<std::size_t>(selected_) < adapters_.size() ? adapters_[static_cast<std::size_t>(selected_)].id : savedAdapterId_.toStdString();
    adapters_ = jlu::adapters(); route_ = jlu::authRoute();
    const auto found = std::find_if(adapters_.begin(), adapters_.end(), [&](const auto& a) { return a.id == previousId; });
    selected_ = found != adapters_.end() ? static_cast<int>(found - adapters_.begin()) : previousId.empty() && adapters_.size() == 1 ? 0 : -1;
    if (demo_ && selected_ < 0) selected_ = 0;
    session_.network(selectedHost(), now()); lastNetwork_ = now(); emit changed();
}
void Controller::connectNow(QString user, QString password, bool remember) {
    if (busy()) return;
    localError_.clear();
    try {
        session_.start({user.trimmed().toUtf8().toStdString(), password.toUtf8().toStdString()}, now());
        if (!demo_) {
            settings_->setValue("username", user.trimmed());
            savedUser_ = user.trimmed();
            if (remember) {
                const auto blob = protect(password.toUtf8(), true);
                if (blob.isEmpty()) log(QStringLiteral("Windows 凭据保护失败，本次未保存密码。"));
                settings_->setValue("credential", blob.toBase64());
                savedPassword_ = blob.isEmpty() ? QString{} : password;
            } else {
                settings_->remove("credential"); savedPassword_.clear();
                QSettings legacy("JluConnect", "JluConnect"); legacy.remove("credential");
            }
            remembered_ = !savedPassword_.isEmpty();
            emit credentialsChanged();
        }
        session_.network(selectedHost(), now()); log(QStringLiteral("开始新的认证流程。"));
    } catch (const jlu::ProtocolError&) { localError_ = QStringLiteral("账号需为 1–36 字节；此协议配置支持 1–16 字节密码。请检查输入。"); log(localError_); }
    emit changed();
}
void Controller::disconnectNow() { localError_.clear(); session_.stop(now()); log(QStringLiteral("手动断开，暂停自动重连。")); emit changed(); }
void Controller::synchronizeTransport() {
    if (generation_ == session_.generation()) return;
    generation_ = session_.generation(); replies_.clear(); dropping_ = false;
    if (!demo_) {
        internetStatus_ = QStringLiteral("未检测"); internetOutcome_ = "unknown"; internetElapsed_ = -1;
        if (probeReply_) { auto* previous = probeReply_.data(); probeReply_ = nullptr; previous->abort(); }
    }
    if (transport_) transport_->close();
}
void Controller::simulate(const jlu::Bytes& p) {
    if (dropping_) return;
    jlu::Bytes reply;
    const auto host = selectedHost();
    if (p[0] == 1) {
        reply.resize(76); reply[0] = 2; std::copy_n(p.begin() + 1, 3, reply.begin() + 1);
        reply[4] = 1; reply[5] = 2; reply[6] = 3; reply[7] = 4;
        if (host) std::copy(host->ip.begin(), host->ip.end(), reply.begin() + 20);
    } else if (p[0] == 3) { reply.resize(39); reply[0] = 4; std::fill(reply.begin() + 23, reply.end(), 0x42); }
    else if (p[0] == 6) { reply.resize(25); reply[0] = 4; }
    else if (p[0] == 0xff) { reply.resize(40); reply[0] = 7; reply[6] = p[36]; reply[7] = p[37]; reply[28] = 0xdc; reply[29] = 2; }
    else if (p[0] == 7) {
        const bool extra = p[6] == 0x0f && p[7] == 0x27;
        reply.resize(extra ? 272 : 40); reply[0] = 7; reply[1] = p[1]; reply[2] = extra ? 0x10 : 40; reply[3] = extra ? 1 : 0;
        reply[4] = 0x0b; reply[5] = extra ? 6 : static_cast<jlu::Byte>(p[5] + 1); reply[8] = p[8]; reply[9] = p[9];
        std::fill_n(reply.begin() + 16, 4, 0x33);
    }
    replies_.push_back({now() + 160, generation_, std::move(reply)});
}
void Controller::pump() {
    const auto time = now();
    if (time - lastTraffic_ >= 1000) sampleTraffic(time);
    if (lastPump_ && time - lastPump_ > 45000 && active()) { resume(); log(QStringLiteral("检测到长时间调度中断，重建认证会话。")); }
    lastPump_ = time;
    if (time - lastNetwork_ >= 2500) refreshNetwork();
    session_.tick(time); synchronizeTransport();
    if (auto packet = session_.takePacket()) {
        if (demo_) simulate(*packet);
        else {
            if (!transport_->isOpen() && session_.host() && !transport_->open(*session_.host())) {
                log(QString::fromStdString(transport_->error())); session_.transportFailed(time);
            } else if (!transport_->send(*packet)) { log(QString::fromStdString(transport_->error())); session_.transportFailed(time); }
        }
    }
    if (demo_) {
        if (!replies_.empty() && replies_.front().at <= time) {
            auto reply = std::move(replies_.front()); replies_.pop_front();
            if (reply.generation == generation_) session_.receive(reply.packet, time);
        }
    } else {
        for (int i = 0; i < 16; ++i) {
            auto packet = transport_->receive(); if (!packet) break;
            if (packet->fromServer) session_.receive(packet->bytes, time);
        }
    }
    if (session_.heartbeatCount() != previousHeartbeatCount_) {
        previousHeartbeatCount_ = session_.heartbeatCount();
        if (previousHeartbeatCount_) lastHeartbeat_ = time;
    }
    const bool stateChanged = lastState_ != session_.state() || lastMessage_ != session_.message();
    if (stateChanged) {
        if (online() && onlineSince_ == 0) onlineSince_ = time;
        if (!online()) onlineSince_ = 0;
        const auto previousStatus = lastState_;
        lastState_ = session_.state(); lastMessage_ = session_.message();
        if (!online() || previousStatus == jlu::State::Login || session_.state() == jlu::State::Online)
            log(status() + QStringLiteral("：") + detail());
    }
    updateNotifications(time);
    if (!demo_ && periodicCheck_ && online() && !checkingInternet() && time - lastInternetCheck_ >= 300000) checkInternet();
    if (stateChanged || time / 1000 != displaySecond_) { displaySecond_ = time / 1000; emit changed(); }
    if (quitRequested_ && session_.state() == jlu::State::Paused) { quitRequested_ = false; emit readyToQuit(); }
}
QString Controller::uptime() const {
    if (!onlineSince_ || !online()) return QStringLiteral("—");
    const auto seconds = (now() - onlineSince_) / 1000;
    return QStringLiteral("%1:%2:%3").arg(seconds / 3600, 2, 10, QChar('0')).arg((seconds / 60) % 60, 2, 10, QChar('0')).arg(seconds % 60, 2, 10, QChar('0'));
}
void Controller::simulateDrop() { if (demo_ && active()) { dropping_ = true; replies_.clear(); log(QStringLiteral("模拟丢包：下一轮心跳将超时，观察自动重连。")); } }
void Controller::suspend() { session_.suspend(); synchronizeTransport(); log(QStringLiteral("进入待机，释放旧会话。")); emit changed(); }
void Controller::resume() { lastPump_ = now(); session_.resume(lastPump_); synchronizeTransport(); refreshNetwork(); log(QStringLiteral("唤醒后重新检查网卡、地址与认证路由。")); emit changed(); }
void Controller::simulateResume() { if (!demo_) return; suspend(); QTimer::singleShot(500, this, &Controller::resume); }
bool Controller::nativeEventFilter(const QByteArray&, void* message, qintptr*) {
    auto* m = static_cast<MSG*>(message);
    if (m->message == RegisterWindowMessageW(L"OpenLnku.ShowWindow")) { emit showWindowRequested(); return true; }
    if (m->message == WM_POWERBROADCAST) {
        if (m->wParam == PBT_APMSUSPEND) suspend();
        else if (m->wParam == PBT_APMRESUMEAUTOMATIC) resume();
    }
    return false;
}
void Controller::log(QString text) {
    logs_.prepend(QDateTime::currentDateTime().toString("HH:mm:ss") + "  " + text);
    while (logs_.size() > 200) logs_.removeLast();
}
void Controller::clearLogs() { logs_.clear(); emit changed(); }
void Controller::requestQuit() {
    if (quitRequested_) return;
    quitRequested_ = true; disconnectNow();
    if (session_.state() == jlu::State::Paused) { quitRequested_ = false; emit readyToQuit(); return; }
    QTimer::singleShot(6500, this, [this] { if (quitRequested_) { quitRequested_ = false; emit readyToQuit(); } });
}
void Controller::exportLogs(const QString& dialogTitle) {
    const auto path = QFileDialog::getSaveFileName(nullptr, dialogTitle, "openlnku.log", "Log (*.log)");
    if (path.isEmpty()) return;
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) file.write(logs_.join('\n').toUtf8());
    else { log(QStringLiteral("日志导出失败，请检查目标目录权限。")); emit changed(); }
}
bool Controller::savePreference(const QString& key, const QVariant& value) {
    if (!settings_) return true;
    const auto level = !demo_ ? jlu::processIntegrity() : 0;
    const bool restricted = level && level < SECURITY_MANDATORY_MEDIUM_RID;
    const bool existed = settings_->contains(key);
    const auto previous = settings_->value(key);
    if (!restricted) {
        settings_->setValue(key, value); settings_->sync();
        if (settings_->status() == QSettings::NoError) return true;
        // Restore the cache too: a failed write must not become an apparent saved value.
        if (existed) settings_->setValue(key, previous); else settings_->remove(key);
        settings_->sync();
    }
    const auto message = restricted
        ? QStringLiteral("当前程序的低完整性权限无法保存设置。请将发布ZIP解压到普通用户目录的新文件夹后运行。")
        : QStringLiteral("设置保存失败，已保留原值。请检查设置存储位置的写入权限。");
    log(message); emit errorRequested(message); return false;
}
void Controller::setAutoLogin(bool enabled) { if (savePreference("autoLogin", enabled)) autoLogin_ = enabled; emit changed(); }
void Controller::setAutoReconnect(bool enabled) { if (savePreference("autoReconnect", enabled)) { autoReconnect_ = enabled; session_.setAutoReconnect(enabled); } emit changed(); }
void Controller::setCloseToTray(bool enabled) { if (savePreference("closeToTray", enabled)) closeToTray_ = enabled; emit changed(); }
void Controller::setNotifications(bool enabled) { if (savePreference("notifications", enabled)) notifications_ = enabled; emit changed(); }
void Controller::setPeriodicCheck(bool enabled) { if (savePreference("periodicCheck", enabled)) periodicCheck_ = enabled; emit changed(); }
void Controller::setProbeUrl(const QString& value) {
    const auto url = QUrl(value.trimmed());
    if (!url.isValid() || url.scheme() != "https" || url.host().isEmpty() || !url.userInfo().isEmpty()) {
        log(QStringLiteral("检测地址需为有效的 HTTPS 地址。")); emit changed(); return;
    }
    if (!savePreference("probeUrl", url.toString())) { emit changed(); return; }
    if (probeReply_) { auto* previous = probeReply_.data(); probeReply_ = nullptr; previous->abort(); }
    probeUrl_ = url.toString(); internetStatus_ = QStringLiteral("未检测");
    internetOutcome_ = "unknown"; internetElapsed_ = -1;
    emit changed();
}
void Controller::connectSaved() {
    if (savedUser_.isEmpty() || savedPassword_.isEmpty()) { emit showWindowRequested(); return; }
    connectNow(savedUser_, savedPassword_, remembered_);
}
void Controller::retryNow() {
    if (!active()) return;
    session_.resume(now()); synchronizeTransport(); refreshNetwork();
    log(QStringLiteral("正在重新建立认证会话。")); emit changed();
}
void Controller::clearCredentials() {
    if (busy()) return;
    session_.forgetCredentials();
    if (settings_) { settings_->remove("username"); settings_->remove("credential"); }
    if (!demo_) {
        QSettings legacy("JluConnect", "JluConnect"); legacy.remove("username"); legacy.remove("credential"); legacy.remove("autoLogin");
    }
    savedUser_.clear(); savedPassword_.clear(); remembered_ = false; setAutoLogin(false);
    emit credentialsChanged(); log(QStringLiteral("已清除保存的账号与密码。")); emit changed();
}
void Controller::checkInternet() {
    if (checkingInternet()) return;
    if (demo_) { internetStatus_ = QStringLiteral("演示未检测"); emit changed(); return; }
    QNetworkProxyFactory::setUseSystemConfiguration(true);
    QNetworkRequest request{QUrl(probeUrl_)}; request.setTransferTimeout(7000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    auto* reply = networkManager_.get(request); probeReply_ = reply;
    const auto started = now();
    const auto requestGeneration = session_.generation();
    const auto requestUrl = probeUrl_;
    internetStatus_ = QStringLiteral("检测中"); internetOutcome_ = "pending"; internetElapsed_ = -1;
    lastInternetCheck_ = now(); emit changed();
    connect(reply, &QNetworkReply::readyRead, this, [reply] { reply->readAll(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, requestGeneration, requestUrl, started] {
        if (probeReply_ != reply || requestUrl != probeUrl_ || requestGeneration != session_.generation()) {
            if (probeReply_ == reply) { probeReply_ = nullptr; internetStatus_ = QStringLiteral("未检测"); internetOutcome_ = "unknown"; internetElapsed_ = -1; emit changed(); }
            reply->deleteLater(); return;
        }
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        internetStatus_ = reply->error() == QNetworkReply::NoError && code >= 200 && code < 300 ? QStringLiteral("访问成功") : QStringLiteral("访问失败");
        internetOutcome_ = reply->error() == QNetworkReply::NoError && code >= 200 && code < 300 ? "success" : "failure";
        internetElapsed_ = static_cast<qint64>(now() - started);
        log(QStringLiteral("互联网检测：%1（HTTP %2）").arg(internetStatus_).arg(code));
        probeReply_ = nullptr; reply->deleteLater(); emit changed();
    });
    QTimer::singleShot(9000, reply, [reply] { if (!reply->isFinished()) reply->abort(); });
}
void Controller::diagnose() {
    refreshNetwork();
    log(QStringLiteral("接入网卡：%1；认证绑定：%2；系统目标路由：%3。").arg(adapterLabel(), bindingLabel(), routeName()));
    log(QStringLiteral("最近心跳：%1；有效心跳：%2。").arg(heartbeatAge()).arg(heartbeats()));
    emit changed();
}
void Controller::copyLogs() { QApplication::clipboard()->setText(logs_.join('\n')); }
void Controller::openLicense() { emit licenseRequested(); }
QString Controller::licenseText() const {
    QString text;
    for (const auto& name : {QStringLiteral("LICENSE"), QStringLiteral("THIRD_PARTY.md")}) {
        QFile file(":/licenses/" + name);
        if (!file.exists()) file.setFileName(QCoreApplication::applicationDirPath() + "/" + name);
        if (file.open(QIODevice::ReadOnly)) text += name + "\n\n" + QString::fromUtf8(file.read(512 * 1024)) + "\n\n";
    }
    return text.isEmpty() ? QStringLiteral("许可文件未找到，请检查安装文件是否完整。") : text;
}
void Controller::reportTrayFailure() {
    const auto level = jlu::processIntegrity();
    const auto message = level && level < SECURITY_MANDATORY_MEDIUM_RID
        ? QStringLiteral("程序正在低完整性权限下运行，无法可靠注册系统托盘或保存开机启动。请将 ZIP 解压到普通用户目录后运行，例如下载目录下的新文件夹。")
        : QStringLiteral("系统托盘图标未能登记，窗口将保持显示。请重试或检查 Windows 资源管理器状态。");
    log(message); emit changed(); emit errorRequested(message);
}
QString Controller::downloadRate() const { return trafficAvailable() ? bytesText(traffic_.receivedRate()) + "/s" : QStringLiteral("—"); }
QString Controller::uploadRate() const { return trafficAvailable() ? bytesText(traffic_.sentRate()) + "/s" : QStringLiteral("—"); }
QString Controller::downloadTotal() const { return bytesText(static_cast<double>(traffic_.receivedTotal())); }
QString Controller::uploadTotal() const { return bytesText(static_cast<double>(traffic_.sentTotal())); }
QString Controller::trafficTotal() const { return bytesText(static_cast<double>(traffic_.receivedTotal()) + static_cast<double>(traffic_.sentTotal())); }
void Controller::sampleTraffic(std::uint64_t time) {
    lastTraffic_ = time;
    std::optional<jlu::TrafficCounters> counters;
    std::string identity;
    if (!demo_ && selected_ >= 0 && static_cast<std::size_t>(selected_) < adapters_.size()) {
        const auto& adapter = adapters_[static_cast<std::size_t>(selected_)];
        identity = adapter.id + ":" + std::to_string(adapter.host.interfaceIndex);
        counters = jlu::trafficCounters(adapter.host.interfaceIndex);
    }
    traffic_.sample(identity, counters, time);
    trafficHistory_.push_back(QVariantMap{{"down", traffic_.receivedRate()}, {"up", traffic_.sentRate()}, {"valid", traffic_.available()}});
    while (trafficHistory_.size() > 45) trafficHistory_.removeFirst();
}
void Controller::updateNotifications(std::uint64_t time) {
    if (!active()) { faultSince_ = 0; faultNotified_ = false; wasOnline_ = false; return; }
    if (online()) {
        if (faultNotified_ && notifications_ && !demo_) emit notificationRequested(QStringLiteral("连接已恢复"), QStringLiteral("认证与心跳已确认。"));
        faultSince_ = 0; faultNotified_ = false; wasOnline_ = true; return;
    }
    if (wasOnline_ && !faultSince_) { faultSince_ = time; internetStatus_ = QStringLiteral("未检测"); internetOutcome_ = "unknown"; internetElapsed_ = -1; }
    if (faultSince_ && time - faultSince_ >= 30000 && !faultNotified_) {
        faultNotified_ = true;
        if (notifications_ && !demo_) emit notificationRequested(QStringLiteral("连接暂未恢复"), detail());
    }
}
bool Controller::startWithWindows() const {
    if (demo_) return true;
    QSettings run("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    const auto command = run.value("OpenLnku").toString();
    const auto executable = '"' + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) + '"';
    return command == executable || command.startsWith(executable + ' ');
}
void Controller::setStartWithWindows(bool enabled) {
    QSettings run("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    if (demo_) return;
    const auto level = jlu::processIntegrity();
    if (level && level < SECURITY_MANDATORY_MEDIUM_RID) {
        const auto message = QStringLiteral("当前程序的低完整性权限无法保存开机启动。请将发布 ZIP 解压到普通用户目录的新文件夹后运行。");
        log(message); emit changed();
        QTimer::singleShot(0, this, [this, message] { emit errorRequested(message); }); return;
    }
    if (enabled) run.setValue("OpenLnku", '"' + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) + '"' + " --tray");
    else if (startWithWindows()) run.remove("OpenLnku");
    run.sync();
    if (run.status() == QSettings::NoError && startWithWindows() == enabled) {
        savePreference("startupConfigured", true); savePreference("startWithWindows", enabled);
        const auto previous = run.value("OpenNex").toString();
        if (previous.startsWith('"') && previous.indexOf('"', 1) > 1) {
            const auto oldExecutable = previous.mid(1, previous.indexOf('"', 1) - 1);
            if (QFileInfo(oldExecutable).fileName().compare("OpenNex.exe", Qt::CaseInsensitive) == 0) { run.remove("OpenNex"); run.sync(); }
        }
        if (run.status() == QSettings::NoError) savePreference("brandStartupMigrated", true);
    } else {
        const auto message = QStringLiteral("无法保存开机自启动设置，请检查当前用户的注册表写入权限。");
        log(message); emit errorRequested(message);
    }
    emit changed();
}
