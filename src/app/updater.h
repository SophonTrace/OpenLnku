#pragma once
#include "update_manifest.h"
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QFile>
#include <QJsonObject>
#include <memory>
#include <functional>
class Updater : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString version READ version NOTIFY changed)
    Q_PROPERTY(QString notes READ notes NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(bool recoveryAvailable READ recoveryAvailable NOTIFY changed)
public:
    explicit Updater(bool demo, QObject* parent = nullptr);
    ~Updater() override;
    QString status() const { return status_; }
    QString version() const { return manifest_.version; }
    QString notes() const { return notes_; }
    bool busy() const { return !reply_.isNull(); }
    bool available() const { return !manifest_.version.isEmpty(); }
    bool ready() const { return ready_; }
    double progress() const { return progress_; }
    bool recoveryAvailable() const { return recovery_; }
    Q_INVOKABLE void check();
    Q_INVOKABLE void download();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void install();
    Q_INVOKABLE void recover();
signals:
    void changed();
    void installationStarted();
private:
    void fetch(const QUrl& url, qsizetype limit, std::function<void(QByteArray)> next);
    void fail(const QString& error);
    void launchHelper(QJsonObject job);
    bool demo_, ready_ = false, recovery_ = false;
    quint64 generation_ = 0;
    double progress_ = 0;
    QString status_ = QStringLiteral("尚未检查"), notes_, archiveUrl_, jobDir_;
    QByteArray manifestBytes_, signature_;
    UpdateManifest manifest_;
    QNetworkAccessManager network_;
    QPointer<QNetworkReply> reply_;
    std::unique_ptr<QFile> download_;
};
