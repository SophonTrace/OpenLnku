#pragma once
#include <QObject>
#include <QJsonObject>
#include <QTimer>
#include <QElapsedTimer>

// A detached process being created does not mean its updater is ready.
// Keep the client alive until the helper has validated and retained its parent.
class UpdateHandoff : public QObject {
    Q_OBJECT
public:
    explicit UpdateHandoff(QObject* parent = nullptr);
    bool pending() const { return pending_; }
    void start(QJsonObject job, const QByteArray& helper, int timeoutMs = 15000);
signals:
    void ready();
    void failed(const QString& message);
private:
    void poll();
    void fail(const QString& message);
    QTimer timer_;
    QElapsedTimer elapsed_;
    QString directory_, token_;
    int timeoutMs_ = 15000;
    bool pending_ = false;
};
