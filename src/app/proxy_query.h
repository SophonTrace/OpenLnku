#pragma once
#include <QNetworkProxy>
#include <QString>
#include <atomic>
// The OS may block during PAC/WPAD discovery. Run it in a disposable child,
// so the UI and network worker can still cancel within a bounded interval.
int runInternalProxyQuery(int argc, char** argv);
QNetworkProxy speedSystemProxy(const QString& host, const std::atomic_bool& cancelled, quint16 port = 443);
QString speedPhysicalDns(const QString& host, quint32 interfaceIndex, const std::atomic_bool& cancelled);
