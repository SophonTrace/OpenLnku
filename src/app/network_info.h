#pragma once
#include "network_transport.h"
#include <QObject>
#include <QVariantMap>
#include <QVariantList>
#include <QThread>
#include <memory>

namespace network_info {
QVariantMap location(const QByteArray& body, const QString& expectedIp);
QVariantList resolvers(const QByteArray& body, const QString& expectedSession);
void mergeResolvers(QVariantList& rows, const QVariantList& observed, const QString& probe);
QString dnsAssessment(const QVariantList& rows, const QString& exitCountry, int observedProbes, bool fakeIp, bool timedOut);
QString assessment(const QVariantList& rows, const QString& exitCountry, bool complete = true);
}

class NetworkInfo final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap info READ info NOTIFY changed)
    Q_PROPERTY(QVariantList dns READ dns NOTIFY changed)
    Q_PROPERTY(bool refreshing READ refreshing NOTIFY changed)
    Q_PROPERTY(bool checkingDns READ checkingDns NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString dnsState READ dnsState NOTIFY changed)
    Q_PROPERTY(QString dnsScope READ dnsScope NOTIFY changed)
public:
    explicit NetworkInfo(bool demo, QObject* parent = nullptr);
    ~NetworkInfo() override;
    QVariantMap info() const { return info_; }
    QVariantList dns() const { return dns_; }
    bool refreshing() const { return job_ && !dnsJob_; }
    bool checkingDns() const { return job_ && dnsJob_; }
    QString error() const { return error_; }
    QString dnsState() const { return dnsState_; }
    QString dnsScope() const { return dnsScope_; }
    Q_INVOKABLE void refresh(int mode, int protocol, quint32 index, const QString& source, bool force = false);
    Q_INVOKABLE void detectDns(int mode, int protocol, quint32 index, const QString& source);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void invalidate();
    Q_INVOKABLE void copyIp();
signals:
    void changed();
private:
    void finish();
    bool demo_, dnsJob_ = false;
    QVariantMap info_;
    QVariantList dns_;
    QString error_, dnsState_ = QStringLiteral("未检测"), dnsScope_, context_;
    qint64 refreshedAt_ = 0;
    quint64 generation_ = 0;
    QThread* job_ = nullptr;
    std::shared_ptr<std::atomic_bool> cancellation_;
};
