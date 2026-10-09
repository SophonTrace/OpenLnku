#include "proxy_query.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxyFactory>
#include <QProcess>
#include <QTextStream>
#include <QUrl>
#include <cstring>
#include <algorithm>
#include <stdexcept>
int runInternalProxyQuery(int argc, char** argv) {
    if (argc < 2 || std::strcmp(argv[1], "--internal-proxy-query")) return -1;
    if (argc != 3 || std::strcmp(argv[2], "speed.cloudflare.com")) return 2;
    QCoreApplication app(argc, argv);
    const auto candidates = QNetworkProxyFactory::systemProxyForQuery(QNetworkProxyQuery(QUrl(QStringLiteral("https://speed.cloudflare.com"))));
    if (candidates.isEmpty()) return 3;
    const auto proxy = candidates.front();
    const QJsonObject object{{"type", int(proxy.type())}, {"host", proxy.hostName()}, {"port", proxy.port()}, {"user", proxy.user()}, {"password", proxy.password()}};
    QTextStream(stdout) << QJsonDocument(object).toJson(QJsonDocument::Compact); return 0;
}
QNetworkProxy speedSystemProxy(const QString& host, const std::atomic_bool& cancelled) {
    QProcess child; child.setProcessChannelMode(QProcess::SeparateChannels);
    child.start(QCoreApplication::applicationFilePath(), {"--internal-proxy-query", host});
    if (!child.waitForStarted(2000)) throw std::runtime_error("无法查询系统代理");
    QElapsedTimer timer; timer.start();
    while (!child.waitForFinished(100)) {
        if (cancelled || timer.elapsed() > 8000 || child.bytesAvailable() > 16384) {
            child.kill(); child.waitForFinished(1000);
            throw std::runtime_error(cancelled ? "cancelled" : "系统代理发现超时");
        }
    }
    if (cancelled) throw std::runtime_error("cancelled");
    const auto bytes = child.readAllStandardOutput();
    if (child.exitStatus() != QProcess::NormalExit || child.exitCode() || bytes.size() > 16384) throw std::runtime_error("系统代理查询失败");
    const auto object = QJsonDocument::fromJson(bytes).object();
    const int type = object.value("type").toInt(-1), port = object.value("port").toInt(-1);
    if (type != QNetworkProxy::NoProxy && type != QNetworkProxy::HttpProxy && type != QNetworkProxy::Socks5Proxy) throw std::runtime_error("系统代理类型不受支持");
    if (type != QNetworkProxy::NoProxy && (object.value("host").toString().isEmpty() || port < 1 || port > 65535)) throw std::runtime_error("系统代理配置无效");
    return QNetworkProxy(QNetworkProxy::ProxyType(type), object.value("host").toString(), quint16(std::max(0, port)), object.value("user").toString(), object.value("password").toString());
}
