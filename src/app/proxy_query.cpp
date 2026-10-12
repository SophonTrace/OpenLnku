#include "proxy_query.h"
#include <winsock2.h>
#include <windows.h>
#include <windns.h>
#include <QHostAddress>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxyFactory>
#include <QProcess>
#include <QTextStream>
#include <QUrl>
#include <QRegularExpression>
#include <cstring>
#include <algorithm>
#include <stdexcept>
int runInternalProxyQuery(int argc, char** argv) {
    if (argc >= 2 && (!std::strcmp(argv[1], "--internal-dns-query") || !std::strcmp(argv[1], "--internal-dns-observe"))) {
        const bool observe = !std::strcmp(argv[1], "--internal-dns-observe");
        if (argc != 5) return 2;
        QCoreApplication app(argc, argv);
        const QString host = QString::fromUtf8(argv[2]); bool ok = false; const quint32 index = QString::fromLatin1(argv[3]).toUInt(&ok);
        if (!ok || (!observe && !index) || host.size() > 253 || !QRegularExpression("^[a-zA-Z0-9.-]+$").match(host).hasMatch()) return 2;
        DNS_QUERY_REQUEST request{}; request.Version = DNS_QUERY_REQUEST_VERSION1; request.QueryName = reinterpret_cast<LPCWSTR>(host.utf16());
        const bool ipv6 = !std::strcmp(argv[4], "6");
        if (std::strcmp(argv[4], "4") && !ipv6) return 2;
        if (observe && !QRegularExpression("^[a-f0-9]{40}-[a-f0-9]{8}\\.ipleak\\.net$").match(host).hasMatch()) return 2;
        request.QueryType = ipv6 ? DNS_TYPE_AAAA : DNS_TYPE_A; request.InterfaceIndex = index; request.QueryOptions = DNS_QUERY_BYPASS_CACHE | DNS_QUERY_NO_HOSTS_FILE | DNS_QUERY_WIRE_ONLY | DNS_QUERY_TREAT_AS_FQDN;
        DNS_QUERY_RESULT result{}; result.Version = DNS_QUERY_RESULTS_VERSION1;
        DNS_QUERY_CANCEL cancel{}; const auto status = DnsQueryEx(&request, &result, &cancel);
        QString address; bool fake = false;
        if (status == ERROR_SUCCESS) {
            for (auto* record = result.pQueryRecords; record; record = record->pNext) {
                if (record->wType != (ipv6 ? DNS_TYPE_AAAA : DNS_TYPE_A)) continue;
                Q_IPV6ADDR v6{}; if (ipv6) std::memcpy(v6.c, record->Data.AAAA.Ip6Address.IP6Byte, 16);
                QHostAddress ip = ipv6 ? QHostAddress(v6) : QHostAddress(ntohl(record->Data.A.IpAddress));
                if (ip.isInSubnet(QHostAddress("198.18.0.0"),15)) fake = true;
                if (ip.isLoopback() || ip.isMulticast() || ip.isNull() || ip.isInSubnet(QHostAddress("198.18.0.0"),15) || ip.isInSubnet(QHostAddress("169.254.0.0"),16)) continue;
                address = ip.toString(); break;
            }
        }
        if (result.pQueryRecords) DnsRecordListFree(result.pQueryRecords, DnsFreeRecordList);
        if (observe) { QTextStream(stdout) << QJsonDocument(QJsonObject{{"status",int(status)},{"fake",fake},{"answer",!address.isEmpty()}}).toJson(QJsonDocument::Compact); return 0; }
        if (address.isEmpty()) return 3;
        QTextStream(stdout) << address; return 0;
    }
    if (argc < 2 || std::strcmp(argv[1], "--internal-proxy-query")) return -1;
    if (argc != 4) return 2;
    const QString host = QString::fromUtf8(argv[2]);
    const QStringList allowed{"speed.cloudflare.com", "speedtest.dukekunshan.edu.cn", "www.speedtest.net", "cloudflare-dns.com", "dns.alidns.com", "speedtest4.cc.shu.edu.cn", "speedtest6.cc.shu.edu.cn", "ipleak.net"};
    if (!allowed.contains(host) && !QRegularExpression("^[a-f0-9]{40}-[a-f0-9]{8}\\.ipleak\\.net$").match(host).hasMatch()) return 2;
    bool portOk = false; const quint16 port = QString::fromLatin1(argv[3]).toUShort(&portOk);
    if (!portOk || (port != 443 && (host != "speedtest.dukekunshan.edu.cn" || port != 8080))) return 2;
    QCoreApplication app(argc, argv);
    QUrl url("https://" + host); url.setPort(port);
    const auto candidates = QNetworkProxyFactory::systemProxyForQuery(QNetworkProxyQuery(url));
    if (candidates.isEmpty()) return 3;
    const auto proxy = candidates.front();
    const QJsonObject object{{"type", int(proxy.type())}, {"host", proxy.hostName()}, {"port", proxy.port()}, {"user", proxy.user()}, {"password", proxy.password()}};
    QTextStream(stdout) << QJsonDocument(object).toJson(QJsonDocument::Compact); return 0;
}
QString speedPhysicalDns(const QString& host, quint32 index, const std::atomic_bool& cancelled, qint64 budgetMs, bool ipv6) {
    if (cancelled) throw std::runtime_error("cancelled");
    if (budgetMs <= 0) throw std::runtime_error("实体网卡 DNS 查询超时");
    budgetMs = std::min<qint64>(2000, budgetMs);
    QElapsedTimer timer; timer.start();
    QProcess child; child.start(QCoreApplication::applicationFilePath(), {"--internal-dns-query",host,QString::number(index),ipv6 ? "6" : "4"});
    if (!child.waitForStarted(int(budgetMs))) throw std::runtime_error("无法查询实体网卡 DNS");
    while (!child.waitForFinished(100)) {
        if (cancelled || timer.elapsed() >= budgetMs) { child.kill(); child.waitForFinished(1000); throw std::runtime_error(cancelled ? "cancelled" : "实体网卡 DNS 查询超时"); }
    }
    if (cancelled) throw std::runtime_error("cancelled");
    const auto bytes = child.readAllStandardOutput().trimmed(); const QString address = QString::fromLatin1(bytes);
    if (child.exitStatus() != QProcess::NormalExit || child.exitCode() || bytes.size() > 64 || QHostAddress(address).protocol() != (ipv6 ? QAbstractSocket::IPv6Protocol : QAbstractSocket::IPv4Protocol)) throw std::runtime_error("实体网卡 DNS 没有有效地址");
    return address;
}
QNetworkProxy speedSystemProxy(const QString& host, const std::atomic_bool& cancelled, quint16 port, qint64 budgetMs) {
    if (cancelled) throw std::runtime_error("cancelled");
    if (budgetMs <= 0) throw std::runtime_error("系统代理发现超时");
    budgetMs = std::min<qint64>(8000, budgetMs);
    QElapsedTimer timer; timer.start();
    QProcess child; child.setProcessChannelMode(QProcess::SeparateChannels);
    child.start(QCoreApplication::applicationFilePath(), {"--internal-proxy-query", host, QString::number(port)});
    if (!child.waitForStarted(int(std::min<qint64>(2000, budgetMs)))) throw std::runtime_error("无法查询系统代理");
    while (!child.waitForFinished(100)) {
        if (cancelled || timer.elapsed() >= budgetMs || child.bytesAvailable() > 16384) {
            child.kill(); child.waitForFinished(1000);
            throw std::runtime_error(cancelled ? "cancelled" : "系统代理发现超时");
        }
    }
    if (cancelled) throw std::runtime_error("cancelled");
    const auto bytes = child.readAllStandardOutput();
    if (child.exitStatus() != QProcess::NormalExit || child.exitCode() || bytes.size() > 16384) throw std::runtime_error("系统代理查询失败");
    const auto object = QJsonDocument::fromJson(bytes).object();
    const int type = object.value("type").toInt(-1), proxyPort = object.value("port").toInt(-1);
    if (type != QNetworkProxy::NoProxy && type != QNetworkProxy::HttpProxy && type != QNetworkProxy::Socks5Proxy) throw std::runtime_error("系统代理类型不受支持");
    if (type != QNetworkProxy::NoProxy && (object.value("host").toString().isEmpty() || proxyPort < 1 || proxyPort > 65535)) throw std::runtime_error("系统代理配置无效");
    return QNetworkProxy(QNetworkProxy::ProxyType(type), object.value("host").toString(), quint16(std::max(0, proxyPort)), object.value("user").toString(), object.value("password").toString());
}

DnsQueryObservation observeSystemDns(const QString& host, quint32 index, bool ipv6, const std::atomic_bool& cancelled, qint64 budgetMs) {
    if (cancelled || budgetMs <= 0) throw std::runtime_error("cancelled");
    QElapsedTimer timer; timer.start();
    QProcess child; child.start(QCoreApplication::applicationFilePath(), {"--internal-dns-observe",host,QString::number(index),ipv6 ? "6" : "4"});
    if (!child.waitForStarted(int(std::min<qint64>(2000,budgetMs)))) throw std::runtime_error("DNS 检测未能启动");
    while (!child.waitForFinished(100)) {
        if (cancelled || timer.elapsed() >= budgetMs) { child.kill(); child.waitForFinished(1000); if (cancelled) throw std::runtime_error("cancelled"); return {true,false,1460}; }
    }
    if (cancelled) throw std::runtime_error("cancelled");
    if (child.exitStatus() != QProcess::NormalExit || child.exitCode()) throw std::runtime_error("DNS 检测未能完成");
    const auto o=QJsonDocument::fromJson(child.readAllStandardOutput()).object();
    if (!o.contains("status")) throw std::runtime_error("DNS 检测响应无效");
    return {o.value("status").toInt()==1460,o.value("fake").toBool(),o.value("status").toInt()};
}
