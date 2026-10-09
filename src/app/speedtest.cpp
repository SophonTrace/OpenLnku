#include "speedtest.h"
#include "proxy_query.h"
#include <winsock2.h>
#include <ws2tcpip.h>
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
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>

namespace {
struct HttpResult { QByteArray body; double elapsed = 0; quint64 bytes = 0; };
using Progress = std::function<void(quint64, double)>;
class Channel {
public:
    Channel(bool direct, quint32 index, QString source, std::atomic_bool& cancel)
        : direct_(direct), index_(index), source_(std::move(source)), cancel_(cancel) {
        WSADATA data{}; if (WSAStartup(MAKEWORD(2,2), &data)) throw std::runtime_error("Windows 网络初始化失败"); total_.start();
    }
    ~Channel() { WSACleanup(); }
    void check() const {
        if (cancel_) throw std::runtime_error("cancelled");
        if (total_.elapsed() > 45000) throw std::runtime_error("测速达到总时限");
    }
    HttpResult request(const QString& host, const QString& address, const QByteArray& path,
                       qint64 upload, qint64 maximum, const Progress& progress = {}) {
        check();
        QSslSocket socket;
        socket.setPeerVerifyMode(QSslSocket::VerifyPeer);
        socket.setPeerVerifyName(host);
        socket.setReadBufferSize(262144);
        if (direct_) {
            QHostAddress destination(address), source(source_);
            if (destination.protocol() != QAbstractSocket::IPv4Protocol || source.protocol() != QAbstractSocket::IPv4Protocol || !index_)
                throw std::runtime_error("实体网卡没有可用的 IPv4 通道");
            SOCKET raw = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
            if (raw == INVALID_SOCKET) throw std::runtime_error("无法创建测速连接");
            const DWORD affinity = htonl(index_);
            sockaddr_in local{}; local.sin_family = AF_INET; local.sin_addr.s_addr = htonl(source.toIPv4Address());
            if (setsockopt(raw, IPPROTO_IP, IP_UNICAST_IF, reinterpret_cast<const char*>(&affinity), sizeof(affinity)) != 0 ||
                ::bind(raw, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
                closesocket(raw); throw std::runtime_error("无法绑定所选实体网卡，未回退到系统路由");
            }
            u_long nonblocking = 1;
            if (ioctlsocket(raw, FIONBIO, &nonblocking)) { closesocket(raw); throw std::runtime_error("无法启用可取消的网络连接"); }
            sockaddr_in remote{}; remote.sin_family = AF_INET; remote.sin_port = htons(443); remote.sin_addr.s_addr = htonl(destination.toIPv4Address());
            int result = ::connect(raw, reinterpret_cast<sockaddr*>(&remote), sizeof(remote));
            if (result && WSAGetLastError() != WSAEWOULDBLOCK) { closesocket(raw); throw std::runtime_error("实体网卡直连失败"); }
            QElapsedTimer connection; connection.start();
            while (result != 0) {
                if (cancel_ || total_.elapsed() > 45000 || connection.elapsed() > 7000) { closesocket(raw); check(); throw std::runtime_error("实体网卡连接超时"); }
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
            sockaddr_in actual{}; int length = sizeof(actual);
            if (getsockname(raw, reinterpret_cast<sockaddr*>(&actual), &length) || actual.sin_addr.s_addr != local.sin_addr.s_addr) {
                closesocket(raw); throw std::runtime_error("测速源地址与所选网卡不一致");
            }
            socket.setProxy(QNetworkProxy::NoProxy);
            if (!socket.setSocketDescriptor(qintptr(raw))) { closesocket(raw); throw std::runtime_error("TLS 通道初始化失败"); }
            socket.startClientEncryption();
        } else {
            if (!proxyResolved_) { proxy_ = speedSystemProxy(host, cancel_); proxyResolved_ = true; }
            check(); socket.setProxy(proxy_);
            socket.connectToHostEncrypted(host, 443);
        }
        QElapsedTimer deadline; deadline.start();
        while (!socket.isEncrypted()) {
            check();
            if (deadline.elapsed() > 8000 || socket.state() == QAbstractSocket::UnconnectedState)
                throw std::runtime_error((QStringLiteral("TLS 连接失败：") + socket.errorString()).toStdString());
            socket.waitForEncrypted(100);
        }
        QByteArray headers = upload ? "POST " : "GET ";
        headers += path + " HTTP/1.1\r\nHost: " + host.toUtf8() + "\r\nUser-Agent: OpenNex/1.1\r\nAccept-Encoding: identity\r\nConnection: close\r\n";
        if (host == "cloudflare-dns.com" || host == "dns.alidns.com") headers += "Accept: application/dns-json\r\n";
        if (upload) headers += "Content-Type: application/octet-stream\r\nContent-Length: " + QByteArray::number(upload) + "\r\n";
        else headers += "Cache-Control: no-cache\r\n";
        headers += "\r\n";
        socket.write(headers);
        QElapsedTimer measured, idle; measured.start(); idle.start();
        qint64 sent = 0;
        QByteArray chunk(65536, '\0');
        while (sent < upload || socket.bytesToWrite()) {
            check();
            if (idle.elapsed() > 6000 || measured.elapsed() > 14000) throw std::runtime_error("上传超时");
            if (sent < upload && socket.bytesToWrite() < 131072) {
                const qint64 count = socket.write(chunk.constData(), std::min<qint64>(chunk.size(), upload - sent));
                if (count < 0) throw std::runtime_error("上传连接中断");
                sent += count;
            }
            if (socket.waitForBytesWritten(100)) idle.restart();
            if (progress && upload) progress(quint64(std::max<qint64>(0, sent - socket.bytesToWrite())), double(measured.elapsed()));
        }
        QByteArray buffer, body;
        bool gotHeader = false, chunked = false;
        qint64 contentLength = -1, remainingChunk = -1, received = 0;
        while (true) {
            check();
            if (idle.elapsed() > 6000 || measured.elapsed() > 14000) throw std::runtime_error("测速节点响应超时");
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
                if (status.size() < 2 || status[1] != "200") throw std::runtime_error("测速节点返回失败状态");
                for (const auto& line : header.split('\n')) {
                    const auto name = line.left(line.indexOf(':')).trimmed().toLower();
                    const auto value = line.mid(line.indexOf(':') + 1).trimmed();
                    if (name == "content-length") { bool ok = false; contentLength = value.toLongLong(&ok); if (!ok || contentLength < 0 || contentLength > maximum) throw std::runtime_error("测速响应长度无效"); }
                    if (name == "transfer-encoding") { if (value.toLower() != "chunked") throw std::runtime_error("测速响应编码不支持"); chunked = true; }
                    if (name == "content-encoding" && value.toLower() != "identity") throw std::runtime_error("测速响应经过压缩");
                }
                buffer.remove(0, end + 4); gotHeader = true;
            }
            bool finished = false;
            QByteArray payload;
            if (chunked) {
                while (true) {
                    if (remainingChunk < 0) {
                        const auto end = buffer.indexOf("\r\n"); if (end < 0) { if (buffer.size() > 128) throw std::runtime_error("分块响应无效"); break; }
                        bool ok = false; remainingChunk = buffer.left(end).split(';').front().toLongLong(&ok, 16);
                        if (!ok || remainingChunk < 0 || remainingChunk > maximum - received) throw std::runtime_error("分块响应长度无效");
                        buffer.remove(0, end + 2);
                        if (!remainingChunk) { finished = true; break; }
                    }
                    if (buffer.size() < remainingChunk + 2) break;
                    payload += buffer.left(remainingChunk);
                    if (buffer.mid(remainingChunk, 2) != "\r\n") throw std::runtime_error("分块响应格式无效");
                    buffer.remove(0, remainingChunk + 2); remainingChunk = -1;
                }
            } else { payload = buffer; buffer.clear(); }
            received += payload.size();
            if (received > maximum || (contentLength >= 0 && received > contentLength)) throw std::runtime_error("测速响应超过限额");
            if (maximum <= 65536) body += payload;
            if (progress && !upload) progress(quint64(received), double(measured.elapsed()));
            if (finished || (!chunked && contentLength >= 0 && received == contentLength)) break;
            if (socket.state() == QAbstractSocket::UnconnectedState) {
                if (chunked || (contentLength >= 0 && received != contentLength)) throw std::runtime_error("测速响应被截断");
                break;
            }
        }
        return {body, double(std::max<qint64>(1, measured.elapsed())), quint64(upload ? upload : received)};
    }
    QString resolve() {
        if (!direct_) return {};
        // Query DNS over the same bound TLS connection, avoiding a TUN fake-IP answer.
        struct Resolver { QString host, address; QByteArray path; };
        const Resolver resolvers[] = {{"dns.alidns.com", "223.5.5.5", "/resolve?name=speed.cloudflare.com&type=A"}, {"cloudflare-dns.com", "1.1.1.1", "/dns-query?name=speed.cloudflare.com&type=A"}};
        for (const auto& resolver : resolvers) {
            try {
                const auto response = request(resolver.host, resolver.address, resolver.path, 0, 65536);
                const auto doc = QJsonDocument::fromJson(response.body);
                for (const auto& item : doc.object().value("Answer").toArray()) {
                    const auto record = item.toObject(); const QHostAddress address(record.value("data").toString());
                    if (record.value("type").toInt() == 1 && address.protocol() == QAbstractSocket::IPv4Protocol) return address.toString();
                }
            } catch (const std::exception&) { check(); }
        }
        throw std::runtime_error("所选实体通道无法访问 DNS 服务或解析测速节点");
    }
private:
    bool direct_, proxyResolved_ = false; QNetworkProxy proxy_; quint32 index_; QString source_; std::atomic_bool& cancel_; QElapsedTimer total_;
};
}

SpeedTest::SpeedTest(bool demo, QObject* parent) : QObject(parent), demo_(demo) {}
SpeedTest::~SpeedTest() { if (cancelled_) *cancelled_ = true; if (worker_) { worker_->wait(); delete worker_; } }
void SpeedTest::setMode(int mode) { if (mode < 0 || mode > 1 || running() || mode == mode_) return; mode_ = mode; emit changed(); }
void SpeedTest::copyExitIp() { if (QHostAddress(exitIp()).isNull()) return; QApplication::clipboard()->setText(exitIp()); }
void SpeedTest::cancel() { if (!worker_) return; if (cancelled_) *cancelled_ = true; results_[size_t(mode_)].phase = QStringLiteral("正在取消"); emit changed(); }
void SpeedTest::preview() {
    if (!demo_ || running()) return;
    auto& state = results_[size_t(mode_)]; state = {};
    state.phase = QStringLiteral("演示结果"); state.ip = mode_ ? "198.51.100.24" : "192.0.2.86";
    state.node = "Cloudflare · HKG"; state.down = 486.2; state.up = 92.4; state.latency = 24; state.jitter = 3;
    state.bytes = 186000000; state.time = "14:32"; state.complete = true;
    for (int i = 0; i < 45; ++i) state.samples.append(QVariantMap{{"down", (1. - std::exp(-i / 5.)) * (486.2 + 5 * std::sin(i))}, {"up", (1. - std::exp(-i / 4.)) * (92.4 + 2 * std::sin(i))}, {"seconds", i * 20. / 44.}, {"valid", true}});
    emit changed();
}
void SpeedTest::start(quint32 index, const QString& source, const QString& adapterName) {
    if (running()) return;
    if (demo_) { preview(); return; }
    auto& state = results_[size_t(mode_)]; state = {}; state.phase = QStringLiteral("检测出口 IP");
    if (mode_ && (!index || QHostAddress(source).protocol() != QAbstractSocket::IPv4Protocol)) { state.phase = QStringLiteral("无法开始"); state.error = QStringLiteral("请选择具有 IPv4 地址的实体网卡"); emit changed(); return; }
    cancelled_ = std::make_shared<std::atomic_bool>(false);
    const auto cancellation = cancelled_; const auto generation = ++generation_; const auto selectedMode = mode_;
    auto deliver = [this, generation, selectedMode](SpeedSnapshot value) {
        QMetaObject::invokeMethod(this, [this, generation, selectedMode, value = std::move(value)] {
            if (generation != generation_) return;
            results_[size_t(selectedMode)] = value; emit changed();
        }, Qt::QueuedConnection);
    };
    worker_ = QThread::create([=] {
        SpeedSnapshot value; quint64 completedBytes = 0;
        try {
            Channel channel(selectedMode == 1, index, source, *cancellation);
            const auto address = channel.resolve();
            const auto trace = channel.request("speed.cloudflare.com", address, "/cdn-cgi/trace", 0, 65536);
            for (const auto& line : trace.body.split('\n')) {
                if (line.startsWith("ip=")) value.ip = QString::fromUtf8(line.mid(3)).trimmed();
                if (line.startsWith("colo=")) value.node = "Cloudflare · " + QString::fromUtf8(line.mid(5)).trimmed();
            }
            if (QHostAddress(value.ip).isNull()) throw std::runtime_error("测速节点没有返回有效出口 IP");
            value.phase = QStringLiteral("测量 HTTP 延迟"); deliver(value);
            std::vector<double> durations;
            for (int i = 0; i < 5; ++i) { auto ping = channel.request("speed.cloudflare.com", address, "/__down?bytes=0", 0, 65536); durations.push_back(ping.elapsed); }
            double jitter = 0; for (size_t i = 1; i < durations.size(); ++i) jitter += std::abs(durations[i] - durations[i - 1]);
            std::sort(durations.begin(), durations.end()); value.latency = durations[2]; value.jitter = jitter / 4;
            QElapsedTimer chartTime; chartTime.start();
            value.samples.append(QVariantMap{{"down", 0.}, {"up", 0.}, {"seconds", 0.}});
            for (int direction = 0; direction < 2; ++direction) {
                value.phase = direction ? QStringLiteral("测量上传速度") : QStringLiteral("测量下载速度"); deliver(value);
                QElapsedTimer phase, sample; phase.start(); sample.start(); quint64 stageBytes = 0;
                const quint64 cap = (direction ? 32ull : 128ull) * 1048576;
                const qint64 chunk = direction ? 4 * 1048576 : 8 * 1048576;
                while (phase.elapsed() < (direction ? 5000 : 8000) && stageBytes < cap) {
                    const qint64 amount = qint64(std::min<quint64>(cap - stageBytes, quint64(chunk)));
                    const auto progress = [&](quint64 current, double) {
                        if (sample.elapsed() < 150) return; sample.restart();
                        const double rate = double(stageBytes + current) * 8. / double(std::max<qint64>(1, phase.elapsed())) / 1000.;
                        if (direction) value.up = rate; else value.down = rate;
                        value.bytes = completedBytes + stageBytes + current;
                        value.samples.append(QVariantMap{{"down", direction ? 0 : value.down}, {"up", direction ? value.up : 0}, {"seconds", chartTime.elapsed() / 1000.}, {"valid", true}});
                        if (value.samples.size() > 100) value.samples.removeFirst(); deliver(value);
                    };
                    const auto response = channel.request("speed.cloudflare.com", address, direction ? QByteArray("/__up") : QByteArray("/__down?bytes=") + QByteArray::number(amount), direction ? amount : 0, direction ? 65536 : amount, progress);
                    if (!direction && response.bytes != quint64(amount)) throw std::runtime_error("下载数据量与请求不一致");
                    stageBytes += response.bytes;
                }
                const double rate = double(stageBytes) * 8. / double(std::max<qint64>(1, phase.elapsed())) / 1000.;
                if (direction) value.up = rate; else value.down = rate;
                completedBytes += stageBytes; value.bytes = completedBytes; deliver(value);
            }
            channel.check(); value.complete = true; value.phase = QStringLiteral("测速完成"); value.time = QDateTime::currentDateTime().toString("HH:mm");
        } catch (const std::exception& exception) {
            value.complete = false;
            if (*cancellation) value.phase = QStringLiteral("已取消");
            else { value.phase = QStringLiteral("测速失败"); value.error = QString::fromUtf8(exception.what()); }
        }
        deliver(value);
        Q_UNUSED(adapterName);
    });
    connect(worker_, &QThread::finished, this, [this, generation] {
        if (generation != generation_) return;
        auto* finished = worker_; worker_ = nullptr; cancelled_.reset(); finished->deleteLater(); emit changed();
    });
    worker_->start(); emit changed();
}
