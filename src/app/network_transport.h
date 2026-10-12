#pragma once
#include <QString>
#include <QByteArray>
#include <atomic>

struct NetworkRoute {
    bool direct = false;
    quint32 index = 0;
    QString source;
    int protocol = 0; // 0 automatic, 1 IPv4, 2 IPv6
};
struct NetworkResponse { QByteArray body; QString route, protocol; };
NetworkRoute networkRoute(bool direct, quint32 index, const QString& ipv4, int protocol);
NetworkResponse networkGet(const NetworkRoute&, const QString& host, const QByteArray& path,
                           std::atomic_bool& cancelled, qint64 budgetMs = 15000, const QString& pinnedAddress = {});
QString networkResolvePhysical(const NetworkRoute&, const QString& host, std::atomic_bool&, qint64 budgetMs = 10000);
bool publicNetworkAddress(const QString& ip);
bool networkObservePhysicalDns(const NetworkRoute&, const QString& host, bool aaaa, std::atomic_bool&, qint64 budgetMs);
