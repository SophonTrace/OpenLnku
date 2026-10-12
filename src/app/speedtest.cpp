#include "speedtest.h"
#include "proxy_query.h"
#include "speed_body.h"
#include "speed_window.h"
#include "network_transport.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QHostInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkProxyFactory>
#include <QSslSocket>
#include <QTimer>
#include <QUrl>
#include <QHash>
#include <QNetworkInterface>
#include <mutex>
#include <chrono>
#include <QRandomGenerator>
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <map>
#include <cstring>
#include <vector>

namespace {
struct HttpResult { QByteArray body; double elapsed = 0; quint64 bytes = 0; QByteArray mime; };
struct Node { QString host, label; QByteArray ping; bool domestic; };
const Node nodes[] = {{"speedtest4.cc.shu.edu.cn", "国内 · 上海大学", "/backend/empty.php", true},
                      {"speed.cloudflare.com", "Cloudflare", "/__down?bytes=0", false}};
using Progress = std::function<void(quint64, double)>;
using Deadline = std::chrono::steady_clock::time_point;
class StageExpired : public std::runtime_error { public: StageExpired() : std::runtime_error("phase complete") {} };
class Channel {
public:
    Channel(bool direct, quint32 index, QString source, std::atomic_bool& cancel,
            Deadline deadline = std::chrono::steady_clock::now() + std::chrono::seconds(75),
            Deadline phaseDeadline = Deadline::max(), int protocol = 0, const std::atomic<Deadline>* dynamicDeadline = nullptr)
        : direct_(direct), index_(index), source_(std::move(source)), cancel_(cancel), deadline_(deadline), phaseDeadline_(phaseDeadline), protocol_(protocol), dynamicDeadline_(dynamicDeadline) {
        WSADATA data{}; if (WSAStartup(MAKEWORD(2,2), &data)) throw std::runtime_error("Windows 网络初始化失败");
    }
    ~Channel() { sockets_.clear(); WSACleanup(); }
    void check() const {
        if (cancel_) throw std::runtime_error("cancelled");
        if (std::chrono::steady_clock::now() >= deadline_) throw std::runtime_error("测速达到总时限");
        if (phaseExpired()) throw StageExpired();
    }
    Deadline deadline() const { return deadline_; }
    qint64 remainingBudget(qint64 limit) const {
        check();
        return std::max<qint64>(0, std::min<qint64>(limit, std::chrono::duration_cast<std::chrono::milliseconds>(deadline_ - std::chrono::steady_clock::now()).count()));
    }
    bool phaseExpired() const { return std::chrono::steady_clock::now() >= (dynamicDeadline_ ? dynamicDeadline_->load() : phaseDeadline_); }
    HttpResult request(const QString& host, const QString& address, const QByteArray& path,
                       qint64 upload, qint64 maximum, const Progress& progress = {}, bool form = false, qint64 windowMs = 0, const QByteArray& expectedMime = {}) {
        check();
        // The domestic server's public Speedtest directory advertises 8080.
        const quint16 port = host == "speedtest.dukekunshan.edu.cn" ? 8080 : 443;
        auto& connectionSlot = sockets_[host];
        if (!connectionSlot || connectionSlot->state() == QAbstractSocket::UnconnectedState) connectionSlot = std::make_unique<QSslSocket>();
        QSslSocket& socket = *connectionSlot;
        const bool reused = socket.isEncrypted();
        socket.setPeerVerifyMode(QSslSocket::VerifyPeer);
        socket.setPeerVerifyName(host);
        socket.setReadBufferSize(4 * 1048576);
        if (!reused && direct_) {
            QHostAddress destination(address), source(source_);
            const bool v6 = source.protocol() == QAbstractSocket::IPv6Protocol;
            if (destination.protocol() != source.protocol() || source.isNull() || !index_)
                throw std::runtime_error("实体网卡没有可用的 IP 通道");
            SOCKET raw = WSASocketW(v6 ? AF_INET6 : AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
            if (raw == INVALID_SOCKET) throw std::runtime_error("无法创建测速连接");
            const DWORD affinity = v6 ? index_ : htonl(index_);
            sockaddr_storage local{}, remote{};
            int addressSize = v6 ? sizeof(sockaddr_in6) : sizeof(sockaddr_in);
            if (v6) {
                DWORD only = 1;
                if (setsockopt(raw, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&only), sizeof(only))) { closesocket(raw); throw std::runtime_error("无法创建 IPv6 通道"); }
                auto* l = reinterpret_cast<sockaddr_in6*>(&local); auto* r = reinterpret_cast<sockaddr_in6*>(&remote);
                l->sin6_family = r->sin6_family = AF_INET6; r->sin6_port = htons(port);
                const auto src = source.toIPv6Address(), dst = destination.toIPv6Address();
                std::memcpy(&l->sin6_addr, src.c, 16); std::memcpy(&r->sin6_addr, dst.c, 16);
            } else {
                auto* l = reinterpret_cast<sockaddr_in*>(&local); auto* r = reinterpret_cast<sockaddr_in*>(&remote);
                l->sin_family = r->sin_family = AF_INET; r->sin_port = htons(port);
                l->sin_addr.s_addr = htonl(source.toIPv4Address()); r->sin_addr.s_addr = htonl(destination.toIPv4Address());
            }
            if (setsockopt(raw, v6 ? IPPROTO_IPV6 : IPPROTO_IP, v6 ? IPV6_UNICAST_IF : IP_UNICAST_IF, reinterpret_cast<const char*>(&affinity), sizeof(affinity)) != 0 ||
                ::bind(raw, reinterpret_cast<sockaddr*>(&local), addressSize) != 0) {
                closesocket(raw); throw std::runtime_error("无法绑定所选实体网卡，未回退到系统路由");
            }
            u_long nonblocking = 1;
            if (ioctlsocket(raw, FIONBIO, &nonblocking)) { closesocket(raw); throw std::runtime_error("无法启用可取消的网络连接"); }
            int result = ::connect(raw, reinterpret_cast<sockaddr*>(&remote), addressSize);
            if (result && WSAGetLastError() != WSAEWOULDBLOCK) { closesocket(raw); throw std::runtime_error("实体网卡直连失败"); }
            QElapsedTimer connection; connection.start();
            while (result != 0) {
                if (cancel_ || std::chrono::steady_clock::now() >= deadline_ || phaseExpired() || connection.elapsed() > 4000) { closesocket(raw); check(); throw std::runtime_error(("实体网卡连接超时：" + host).toStdString()); }
                fd_set write, errors; FD_ZERO(&write); FD_ZERO(&errors); FD_SET(raw, &write); FD_SET(raw, &errors);
                timeval timeout{0, 100000}; result = select(0, nullptr, &write, &errors, &timeout);
                if (result < 0 || FD_ISSET(raw, &errors)) { closesocket(raw); throw std::runtime_error("实体网卡连接不可用"); }
                if (!result) { result = -1; continue; }
                int error = 0, length = sizeof(error);
                if (getsockopt(raw, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) || error) {
                    closesocket(raw); throw std::runtime_error("实体网卡连接失败");
                }
                result = 0;
            }
            sockaddr_storage actual{}; int length = sizeof(actual);
            if (getsockname(raw, reinterpret_cast<sockaddr*>(&actual), &length) ||
                (v6 ? std::memcmp(&reinterpret_cast<sockaddr_in6*>(&actual)->sin6_addr, &reinterpret_cast<sockaddr_in6*>(&local)->sin6_addr,16) != 0 : reinterpret_cast<sockaddr_in*>(&actual)->sin_addr.s_addr != reinterpret_cast<sockaddr_in*>(&local)->sin_addr.s_addr)) {
                closesocket(raw); throw std::runtime_error("测速源地址与所选网卡不一致");
            }
            socket.setProxy(QNetworkProxy::NoProxy);
            if (!socket.setSocketDescriptor(qintptr(raw))) { closesocket(raw); throw std::runtime_error("TLS 通道初始化失败"); }
            socket.startClientEncryption();
        } else if (!reused) {
            if (!proxies_.contains(host)) proxies_.insert(host, speedSystemProxy(host, cancel_, port, remainingBudget(8000)));
            check(); socket.setProxy(proxies_.value(host));
            if (protocol_ && proxies_.value(host).type() != QNetworkProxy::NoProxy) throw std::runtime_error("显式代理无法固定测速目标的 IP 协议");
            socket.connectToHostEncrypted(address.isEmpty() ? host : address, port, host, QIODevice::ReadWrite, protocol_ == 1 ? QAbstractSocket::IPv4Protocol : protocol_ == 2 ? QAbstractSocket::IPv6Protocol : QAbstractSocket::AnyIPProtocol);
        }
        QElapsedTimer deadline; deadline.start();
        while (!socket.isEncrypted()) {
            check();
            if (deadline.elapsed() > 5000 || socket.state() == QAbstractSocket::UnconnectedState)
                throw std::runtime_error((QStringLiteral("TLS 连接失败：") + host + " · " + socket.errorString()).toStdString());
            socket.waitForEncrypted(100);
        }
        QByteArray headers = upload ? "POST " : "GET ";
        headers += path + " HTTP/1.1\r\nHost: " + host.toUtf8() + (port == 443 ? QByteArray() : ":" + QByteArray::number(port)) + "\r\nUser-Agent: OpenLnku/1.1\r\nAccept-Encoding: identity\r\nConnection: keep-alive\r\n";
        if (host == "cloudflare-dns.com" || host == "dns.alidns.com") headers += "Accept: application/dns-json\r\n";
        if (upload) headers += QByteArray("Content-Type: ") + (form ? "application/x-www-form-urlencoded" : "application/octet-stream") + "\r\nContent-Length: " + QByteArray::number(upload) + "\r\n";
        else headers += "Cache-Control: no-cache\r\n";
        headers += "\r\n";
        socket.write(headers);
        QElapsedTimer measured, idle; measured.start(); idle.start();
        qint64 sent = 0;
        QByteArray chunk(65536, '\0');
        for (auto& byte : chunk) byte = char('A' + QRandomGenerator::global()->bounded(26));
        if (form) { socket.write("content1="); sent = 9; }
        while (sent < upload || socket.bytesToWrite()) {
            check();
            if (idle.elapsed() > 10000 || measured.elapsed() > 20000) throw std::runtime_error("上传超时");
            if (sent < upload && socket.bytesToWrite() < 131072) {
                const qint64 count = socket.write(chunk.constData(), std::min<qint64>(chunk.size(), upload - sent));
                if (count < 0) throw std::runtime_error("上传连接中断");
                sent += count;
            }
            if (socket.waitForBytesWritten(100)) idle.restart();
            if (progress && upload) progress(quint64(std::max<qint64>(0, sent - socket.bytesToWrite())), double(measured.elapsed()));
        }
        if (upload && qEnvironmentVariableIsSet("OPENLNKU_SPEED_DIAGNOSTIC")) qInfo("speed upload: sent=%lld plainPending=%lld tlsPending=%lld", sent, socket.bytesToWrite(), socket.encryptedBytesToWrite());
        QByteArray buffer, body, mime;
        std::unique_ptr<SpeedBody> decoder;
        bool partial = false;
        bool gotHeader = false, chunked = false, close = false;
        qint64 contentLength = -1, received = 0;
        while (true) {
            // A timed download intentionally ends mid-body, after counting the
            // bytes actually received. Uploads only count confirmed requests.
            if (!upload && phaseExpired() && gotHeader && received) { partial = true; break; }
            check();
            if (!upload && windowMs && measured.elapsed() >= windowMs && gotHeader && received) { partial = true; break; }
            if (idle.elapsed() > 10000 || measured.elapsed() > 20000) throw std::runtime_error(upload ? "测速节点上传响应超时" : "测速节点下载响应超时");
            if (!socket.bytesAvailable()) socket.waitForReadyRead(100);
            const QByteArray bytes = socket.readAll();
            if (!bytes.isEmpty()) { idle.restart(); buffer += bytes; }
            if (!gotHeader) {
                const auto end = buffer.indexOf("\r\n\r\n");
                if (end < 0) {
                    if (buffer.size() > 16384) throw std::runtime_error("测速响应头过大");
                    if (socket.state() == QAbstractSocket::UnconnectedState) throw std::runtime_error("测速节点没有有效响应");
                    continue;
                }
                const auto header = buffer.left(end); const auto status = header.left(header.indexOf("\r\n")).split(' ');
                if (upload && qEnvironmentVariableIsSet("OPENLNKU_SPEED_DIAGNOSTIC")) qInfo("speed upload: HTTP status=%s", status.size() >= 2 ? status[1].constData() : "invalid");
                if (status.size() < 2 || status[1] != "200") throw std::runtime_error("测速节点返回失败状态");
                for (const auto& line : header.split('\n')) {
                    const auto name = line.left(line.indexOf(':')).trimmed().toLower();
                    const auto value = line.mid(line.indexOf(':') + 1).trimmed();
                    if (name == "content-length") { bool ok = false; contentLength = value.toLongLong(&ok); if (!ok || contentLength < 0 || contentLength > maximum) throw std::runtime_error("测速响应长度无效"); }
                    if (name == "transfer-encoding") { if (value.toLower() != "chunked") throw std::runtime_error("测速响应编码不支持"); chunked = true; }
                    if (name == "content-encoding" && value.toLower() != "identity") throw std::runtime_error("测速响应经过压缩");
                    if (name == "content-type") mime = value.toLower();
                    if (name == "connection" && value.toLower().contains("close")) close = true;
                }
                if (!expectedMime.isEmpty() && !mime.startsWith(expectedMime)) throw std::runtime_error("国内节点未返回测速文件");
                buffer.remove(0, end + 4); gotHeader = true;
                decoder = std::make_unique<SpeedBody>(chunked, chunked ? -1 : contentLength, maximum);
            }
            const auto payload = decoder->feed(std::move(buffer)); buffer.clear();
            received = decoder->received();
            if (maximum <= 65536) body += payload;
            if (progress && !upload) progress(quint64(received), double(measured.elapsed()));
            if (decoder->complete()) break;
            if (socket.state() == QAbstractSocket::UnconnectedState) { decoder->finishEof(); break; }
        }
        // Reuse only responses with an unambiguous fixed body boundary. Chunked
        // trailers or EOF-framed bodies must not leak into the next response.
        if (partial || chunked || contentLength < 0 || close) { socket.disconnectFromHost(); if (socket.state() != QAbstractSocket::UnconnectedState) socket.waitForDisconnected(100); socket.abort(); }
        return {body, double(std::max<qint64>(1, measured.elapsed())), quint64(upload ? upload : received), mime};
    }
    QString route(const QString& host) const {
        if (direct_) return QStringLiteral("实体网卡直连");
        if (proxies_.value(host, QNetworkProxy::NoProxy).type() != QNetworkProxy::NoProxy) return QStringLiteral("系统代理");
        const auto it = sockets_.find(host);
        if (it != sockets_.end()) {
            const auto local = it->second->localAddress();
            for (const auto& adapter : QNetworkInterface::allInterfaces())
                for (const auto& entry : adapter.addressEntries())
                    if (entry.ip() == local) return adapter.humanReadableName();
        }
        return QStringLiteral("系统路由");
    }
    void seedProxy(const QString& host, const QNetworkProxy& proxy) { proxies_.insert(host, proxy); }
    QNetworkProxy proxy(const QString& host) const { return proxies_.value(host, QNetworkProxy::NoProxy); }
    QString resolve(const QString& host) {
        if (!direct_) return {};
        if (addresses_.contains(host)) return addresses_.value(host);
        const bool v6 = QHostAddress(source_).protocol() == QAbstractSocket::IPv6Protocol;
        try {
            const auto address = speedPhysicalDns(host, index_, cancel_, remainingBudget(1500), v6);
            if (publicNetworkAddress(address)) { addresses_.insert(host,address); return address; }
        } catch (const std::exception&) { check(); }
        // Bootstrap HTTPS DNS on the SAME physical adapter, never on a proxy.
        NetworkRoute dnsRoute = networkRoute(true,index_,source_,0);
        Channel dns(true,dnsRoute.index,dnsRoute.source,cancel_,deadline_,phaseDeadline_,dnsRoute.protocol);
        const bool dnsV6 = QHostAddress(dnsRoute.source).protocol() == QAbstractSocket::IPv6Protocol;
        const QStringList bootstrap = dnsV6 ? QStringList{"2400:3200::1","2400:3200:baba::1"} : QStringList{"223.5.5.5","223.6.6.6"};
        for (const auto& peer : bootstrap) {
            try {
                const auto response = dns.request("dns.alidns.com", peer, "/resolve?name=" + QUrl::toPercentEncoding(host) + (v6 ? "&type=AAAA" : "&type=A"), 0,65536);
                const auto document = QJsonDocument::fromJson(response.body);
                if (document.object().value("Status").toInt(-1) != 0) continue;
                for (const auto& item : document.object().value("Answer").toArray()) {
                    const auto record = item.toObject(); const QString ip = record.value("data").toString();
                    if (record.value("type").toInt() == (v6 ? 28 : 1) && publicNetworkAddress(ip)) { addresses_.insert(host,ip); return ip; }
                }
            } catch (const std::exception&) { check(); }
        }
        throw std::runtime_error("无法取得测速节点的真实 IP 地址");
    }
    QString actualProtocol(const QString& host) const {
        const auto it = sockets_.find(host);
        if (it == sockets_.end() || proxies_.value(host,QNetworkProxy::NoProxy).type() != QNetworkProxy::NoProxy) return QStringLiteral("由代理决定");
        return it->second->peerAddress().protocol() == QAbstractSocket::IPv6Protocol ? "IPv6" : "IPv4";
    }

private:
    bool direct_; QHash<QString,QNetworkProxy> proxies_; QHash<QString,QString> addresses_; std::map<QString,std::unique_ptr<QSslSocket>> sockets_; quint32 index_; QString source_; std::atomic_bool& cancel_; Deadline deadline_, phaseDeadline_; int protocol_; const std::atomic<Deadline>* dynamicDeadline_;
};
}

bool publicNetworkAddress(const QString& text) {
    const QHostAddress ip(text);
    if (ip.protocol() == QAbstractSocket::IPv6Protocol) return ip.isInSubnet(QHostAddress("2000::"),3) && !ip.isInSubnet(QHostAddress("2001:db8::"),32);
    if (ip.protocol() != QAbstractSocket::IPv4Protocol || ip.isNull() || ip.isLoopback() || ip.isMulticast()) return false;
    const std::pair<const char*,int> blocks[] = {{"0.0.0.0",8},{"10.0.0.0",8},{"100.64.0.0",10},{"127.0.0.0",8},{"169.254.0.0",16},{"172.16.0.0",12},{"192.168.0.0",16},{"198.18.0.0",15},{"224.0.0.0",3},{"192.0.2.0",24},{"198.51.100.0",24},{"203.0.113.0",24}};
    for (const auto& block : blocks) if (ip.isInSubnet(QHostAddress(block.first),block.second)) return false;
    return true;
}
NetworkRoute networkRoute(bool direct, quint32 index, const QString& ipv4, int protocol) {
    if (protocol < 0 || protocol > 2) throw std::runtime_error("无效 IP 协议");
    if (!direct) return {false,0,{},protocol};
    if (!index) throw std::runtime_error("请选择接入网卡");
    ULONG size = 16384; std::vector<unsigned char> buffer(size);
    auto status = GetAdaptersAddresses(AF_UNSPEC,GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST|GAA_FLAG_SKIP_DNS_SERVER,nullptr,reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()),&size);
    if (status == ERROR_BUFFER_OVERFLOW) { buffer.resize(size); status = GetAdaptersAddresses(AF_UNSPEC,GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST|GAA_FLAG_SKIP_DNS_SERVER,nullptr,reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()),&size); }
    if (status != NO_ERROR) throw std::runtime_error("无法读取接入网卡");
    QString v4,v6; quint32 v4Index = 0,v6Index = 0;
    for (auto* adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()); adapter; adapter = adapter->Next) {
        if (adapter->IfIndex != index && adapter->Ipv6IfIndex != index) continue;
        if (adapter->OperStatus != IfOperStatusUp) throw std::runtime_error("接入网卡未连接");
        v4Index = adapter->IfIndex; v6Index = adapter->Ipv6IfIndex;
        for (auto* entry = adapter->FirstUnicastAddress; entry; entry = entry->Next) {
            if (entry->DadState != IpDadStatePreferred || !entry->PreferredLifetime) continue;
            const auto* addr = entry->Address.lpSockaddr;
            if (addr->sa_family == AF_INET) { const QString value = QHostAddress(ntohl(reinterpret_cast<const sockaddr_in*>(addr)->sin_addr.s_addr)).toString(); if (value == ipv4 || ipv4.isEmpty()) v4 = value; }
            if (addr->sa_family == AF_INET6) { Q_IPV6ADDR bytes{}; std::memcpy(bytes.c,&reinterpret_cast<const sockaddr_in6*>(addr)->sin6_addr,16); const QHostAddress value(bytes); if (publicNetworkAddress(value.toString())) v6 = value.toString(); }
        }
        break;
    }
    if (protocol != 1 && !v6.isEmpty()) return {true,v6Index,v6,2};
    if (protocol != 2 && !v4.isEmpty()) return {true,v4Index,v4,1};
    throw std::runtime_error("所选网卡没有该协议的可用地址");
}
NetworkResponse networkGet(const NetworkRoute& route, const QString& host, const QByteArray& path, std::atomic_bool& cancelled, qint64 budgetMs, const QString& pinnedAddress) {
    if (!pinnedAddress.isEmpty() && !publicNetworkAddress(pinnedAddress)) throw std::runtime_error("无效检测服务器地址");
    if (budgetMs <= 0 || cancelled) throw std::runtime_error("cancelled");
    Channel channel(route.direct,route.index,route.source,cancelled,std::chrono::steady_clock::now()+std::chrono::milliseconds(budgetMs),Deadline::max(),route.protocol);
    const auto result = channel.request(host,pinnedAddress.isEmpty()?channel.resolve(host):pinnedAddress,path,0,65536);
    return {result.body,channel.route(host),channel.actualProtocol(host)};
}

SpeedTest::SpeedTest(bool demo, QObject* parent) : QObject(parent), demo_(demo) {}
SpeedTest::~SpeedTest() { if (cancelled_) *cancelled_ = true; if (worker_) { worker_->wait(); delete worker_; } }
void SpeedTest::setMode(int mode) { if (mode < 0 || mode > 1 || running() || mode == mode_) return; mode_ = mode; emit changed(); }
void SpeedTest::setNodeChoice(int choice) { if (choice < 0 || choice > 2 || running() || choice == nodeChoice_) return; nodeChoice_ = choice; emit changed(); }
void SpeedTest::setProtocolChoice(int choice) { if (choice < 0 || choice > 2 || running() || choice == protocolChoice_) return; protocolChoice_ = choice; emit changed(); }
void SpeedTest::copyExitIp() { if (QHostAddress(exitIp()).isNull()) return; QApplication::clipboard()->setText(exitIp()); }
void SpeedTest::cancel() { if (!worker_) return; if (cancelled_) *cancelled_ = true; results_[slot()].phase = QStringLiteral("正在取消"); emit changed(); }
void SpeedTest::preview() {
    if (!demo_ || running()) return;
    auto& state = results_[slot()]; state = {};
    state.phase = QStringLiteral("演示结果"); state.ip = mode_ ? "198.51.100.24" : "192.0.2.86";
    state.route = mode_ ? QStringLiteral("实体网卡直连") : QStringLiteral("系统代理"); state.ipSource = "speed.cloudflare.com";
    state.protocol = protocolChoice_ == 2 ? "IPv6" : "IPv4";
    state.node = nodeChoice_ == 1 ? "国内 · 上海大学" : "Cloudflare · HKG"; state.down = 486.2; state.up = 92.4; state.latency = 24; state.jitter = 3;
    state.bytes = 186000000; state.time = "14:32"; state.complete = true;
    for (int i = 0; i < 45; ++i) state.samples.append(QVariantMap{{"down", (1. - std::exp(-i / 5.)) * (486.2 + 5 * std::sin(i))}, {"up", (1. - std::exp(-i / 4.)) * (92.4 + 2 * std::sin(i))}, {"seconds", i * 20. / 44.}, {"valid", true}});
    emit changed();
}
void SpeedTest::start(quint32 index, const QString& source, const QString& adapterName) {
    if (running()) return;
    if (demo_) { preview(); return; }
    auto& state = results_[slot()]; state = {}; state.phase = QStringLiteral("选择测速节点");
    if (mode_ && (!index || QHostAddress(source).protocol() != QAbstractSocket::IPv4Protocol)) { state.phase = QStringLiteral("无法开始"); state.error = QStringLiteral("请选择具有 IPv4 地址的实体网卡"); emit changed(); return; }
    cancelled_ = std::make_shared<std::atomic_bool>(false);
    const auto cancellation = cancelled_; const auto generation = ++generation_; const auto selectedMode = mode_;
    const auto selectedChoice = nodeChoice_; const auto selectedSlot = slot(); const auto protocol = protocolChoice_; const bool extended = extended_;
    auto deliver = [this, generation, selectedSlot](SpeedSnapshot value) {
        QMetaObject::invokeMethod(this, [this, generation, selectedSlot, value = std::move(value)] {
            if (generation != generation_) return;
            results_[selectedSlot] = value; emit changed();
        }, Qt::QueuedConnection);
    };
    worker_ = QThread::create([=] {
        SpeedSnapshot value; quint64 completedBytes = 0;
        try {
            const auto globalDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(110);
            auto route = networkRoute(selectedMode==1,index,source,protocol);
            auto channel = std::make_unique<Channel>(route.direct,route.index,route.source,*cancellation,globalDeadline,Deadline::max(),route.protocol);
            QString country;
            value.phase = QStringLiteral("检测出口 IP"); deliver(value);
            try {
                HttpResult trace;
                try { trace=channel->request("speed.cloudflare.com",channel->resolve("speed.cloudflare.com"),"/cdn-cgi/trace",0,65536); }
                catch (const std::exception&) {
                    if (*cancellation || protocol || !selectedMode || route.protocol!=2) throw;
                    route=networkRoute(true,index,source,1);
                    channel=std::make_unique<Channel>(true,route.index,route.source,*cancellation,globalDeadline,Deadline::max(),1);
                    trace=channel->request("speed.cloudflare.com",channel->resolve("speed.cloudflare.com"),"/cdn-cgi/trace",0,65536);
                }
                for (const auto& line : trace.body.split('\n')) {
                    if (line.startsWith("ip=")) value.ip=QString::fromUtf8(line.mid(3)).trimmed();
                    if (line.startsWith("loc=")) country=QString::fromUtf8(line.mid(4)).trimmed();
                    if (line.startsWith("colo=")) value.node="Cloudflare · "+QString::fromUtf8(line.mid(5)).trimmed();
                }
                if (!publicNetworkAddress(value.ip)) value.ip=QStringLiteral("—");
                value.ipSource="speed.cloudflare.com";
            } catch (const std::exception&) { channel->check(); value.error=QStringLiteral("出口 IP 查询暂不可用"); }
            if (!route.direct && protocol==0 && value.ip!=QStringLiteral("—") && channel->proxy("speed.cloudflare.com").type()==QNetworkProxy::NoProxy) {
                route.protocol=value.ip.contains(':')?2:1;
                channel=std::make_unique<Channel>(false,route.index,route.source,*cancellation,globalDeadline,Deadline::max(),route.protocol);
            }
            const int preferred = country=="CN" ? 0 : !country.isEmpty() ? 1 : selectedMode ? 0 : 1;
            int chosen=-1; QString address; Node node;
            for (int attempt=0;attempt<2;++attempt) {
                const int candidate=selectedChoice ? selectedChoice-1 : (attempt ? 1-preferred : preferred);
                if (selectedChoice && attempt) break;
                try {
                    node=nodes[candidate];
                    const bool v6=route.direct ? route.protocol==2 : value.ip.contains(':');
                    if (candidate==0 && v6) node.host="speedtest6.cc.shu.edu.cn";
                    const auto resolved=channel->resolve(node.host);
                    channel->request(node.host,resolved,node.ping,0,65536);
                    chosen=candidate; address=resolved; break;
                } catch (const std::exception&) { channel->check(); if (selectedChoice) throw; }
            }
            if (chosen<0) throw std::runtime_error("当前通道没有可用测速节点");
            if (node.domestic || value.node==QStringLiteral("—")) value.node=node.label;
            value.route=selectedMode ? adapterName : channel->route(node.host);
            value.protocol=channel->actualProtocol(node.host);
            value.bytes=completedBytes;
            deliver(value);
            value.phase = QStringLiteral("测量 HTTP 延迟"); deliver(value);
            std::vector<double> durations;
            for (int i = 0; i < 5; ++i) { auto ping = channel->request(node.host, address, node.ping, 0, 65536); durations.push_back(ping.elapsed); }
            double jitter = 0; for (size_t i = 1; i < durations.size(); ++i) jitter += std::abs(durations[i] - durations[i - 1]);
            std::sort(durations.begin(), durations.end()); value.latency = durations[2]; value.jitter = jitter / 4;
            QElapsedTimer chartTime; chartTime.start();
            value.samples.append(QVariantMap{{"down", 0.}, {"up", 0.}, {"seconds", 0.}});
            for (int direction = 0; direction < 2; ++direction) {
                value.uploading=direction!=0;
                value.phase=direction ? QStringLiteral("预热上传通道") : QStringLiteral("预热下载通道"); deliver(value);
                constexpr int lanes=4;
                const qint64 duration=direction ? (extended ? 15000 : 7000) : (extended ? 20000 : 10000);
                const quint64 laneBudget=1024ull*1048576;
                std::array<quint64,lanes> bytes{},confirmed{},baseline{};
                std::array<QString,lanes> failures{};
                std::array<std::unique_ptr<QThread>,lanes> transfers;
                std::mutex lock; bool warmCaptured=false; SpeedWindow window(duration);
                QElapsedTimer sample; sample.start();
                const auto proxy=channel->proxy(node.host);
                auto update=[&](int lane,quint64 count) {
                    std::lock_guard guard(lock); bytes[lane]=count;
                    if (!warmCaptured && window.begin(std::chrono::steady_clock::now(),std::all_of(bytes.begin(),bytes.end(),[](quint64 b){return b>=32768;}))) { baseline=bytes; warmCaptured=true; value.phase=direction?QStringLiteral("测量上传速度"):QStringLiteral("测量下载速度"); }
                    if (!warmCaptured || sample.elapsed()<150) return; sample.restart();
                    quint64 total=0; for (int i=0;i<lanes;++i) total+=bytes[i]-baseline[i];
                    const double rate=double(total)*8./double(std::max<qint64>(1,window.measuredMs()))/1000.;
                    if (direction) value.up=rate; else value.down=rate;
                    quint64 all=0; for (const auto b:bytes) all+=b;
                    value.bytes=completedBytes+all;
                    value.samples.append(QVariantMap{{"down",direction?0:value.down},{"up",direction?value.up:0},{"seconds",chartTime.elapsed()/1000.},{"valid",true}});
                    if (value.samples.size()>250) value.samples.removeFirst(); deliver(value);
                };
                for (int lane=0;lane<lanes;++lane) {
                    transfers[lane].reset(QThread::create([&,lane] {
                        quint64 transferred=0;
                        try {
                            Channel stream(route.direct,route.index,route.source,*cancellation,globalDeadline,Deadline::max(),route.protocol,window.deadlineSignal());
                            stream.seedProxy(node.host,proxy);
                            qint64 uploadSize=65536;
                            while (!stream.phaseExpired() && transferred<laneBudget) {
                                const quint64 left=laneBudget-transferred;
                                const qint64 amount=qint64(std::min<quint64>(left,direction?quint64(uploadSize):64ull*1048576));
                                if (!direction && node.domestic && left<64ull*1048576) throw std::runtime_error("测速达到流量上限");
                                const QByteArray path=node.domestic ? (direction?QByteArray("/backend/empty.php"):QByteArray("/backend/garbage.php?ckSize=64&r=")+QByteArray::number(QDateTime::currentMSecsSinceEpoch())) : (direction?QByteArray("/__up"):QByteArray("/__down?bytes=")+QByteArray::number(amount));
                                const auto response=stream.request(node.host,address,path,direction?amount:0,direction?65536:qint64(left),[&](quint64 current,double){update(lane,transferred+current);},false,
                                    0,!direction?QByteArray("application/octet-stream"):QByteArray());
                                if (!response.bytes) throw std::runtime_error("测速节点未提供有效传输数据");
                                transferred+=response.bytes; { std::lock_guard guard(lock); confirmed[lane]=transferred; }
                                update(lane,transferred);
                                if (direction) uploadSize=std::clamp<qint64>(qint64(amount*350./response.elapsed),16384,2*1048576);
                            }
                            if (transferred>=laneBudget && !stream.phaseExpired()) throw std::runtime_error("测速达到流量上限");
                        } catch (const StageExpired&) {}
                        catch (const std::exception& e) { failures[lane]=QString::fromUtf8(e.what()); }
                    })); transfers[lane]->start();
                }
                for (auto& transfer:transfers) transfer->wait();
                channel->check();
                quint64 stageBytes=0,consumed=0; QString failure;
                for (int lane=0;lane<lanes;++lane) {
                    consumed+=bytes[lane];
                    if (!failures[lane].isEmpty()) failure=failures[lane];
                    if (!warmCaptured || baseline[lane]<32768) failure=QStringLiteral("测速连接预热未完成");
                    const auto accepted=direction?confirmed[lane]:bytes[lane];
                    if (accepted>baseline[lane]) stageBytes+=accepted-baseline[lane];
                }
                completedBytes+=consumed; value.bytes=completedBytes;
                if (!failure.isEmpty()) throw std::runtime_error(failure.toStdString());
                const auto elapsed=window.measuredMs();
                if (!stageBytes || elapsed<duration-200 || elapsed>duration+1500) throw std::runtime_error("测速未完成完整测量窗口");
                const double rate=double(stageBytes)*8./double(elapsed)/1000.;
                if (direction) value.up=rate; else value.down=rate;
                deliver(value);
            }
            channel->check(); value.uploading=false; value.complete = true; value.phase = QStringLiteral("测速完成"); value.time = QDateTime::currentDateTime().toString("HH:mm");
        } catch (const std::exception& exception) {
            value.complete = false;
            if (*cancellation) value.phase = QStringLiteral("已取消");
            else { value.phase = QStringLiteral("测速失败"); value.error = QString::fromUtf8(exception.what()); }
        }
        deliver(value);
    });
    connect(worker_, &QThread::finished, this, [this, generation] {
        if (generation != generation_) return;
        auto* finished = worker_; worker_ = nullptr; cancelled_.reset(); finished->deleteLater(); emit changed();
    });
    worker_->start(); emit changed();
}

QString networkResolvePhysical(const NetworkRoute& route,const QString& host,std::atomic_bool& cancelled,qint64 budgetMs) {
    if (!route.direct || budgetMs<=0 || cancelled) throw std::runtime_error("invalid resolver route");
    Channel channel(true,route.index,route.source,cancelled,std::chrono::steady_clock::now()+std::chrono::milliseconds(budgetMs),Deadline::max(),route.protocol);
    return channel.resolve(host);
}
