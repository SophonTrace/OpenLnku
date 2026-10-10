#pragma once
#include "core/session.h"
#include "platform/windows_network.h"
#include <QObject>
#include <QAbstractNativeEventFilter>
#include <QElapsedTimer>
#include <QSettings>
#include <QTimer>
#include <QVariantList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <deque>
#include <memory>

class Controller : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString detail READ detail NOTIFY changed)
    Q_PROPERTY(bool online READ online NOTIFY changed)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool demo READ demo CONSTANT)
    Q_PROPERTY(QString adapterLabel READ adapterLabel NOTIFY changed)
    Q_PROPERTY(QString ip READ ip NOTIFY changed)
    Q_PROPERTY(QString routeLabel READ routeLabel NOTIFY changed)
    Q_PROPERTY(bool routeConflict READ routeConflict NOTIFY changed)
    Q_PROPERTY(QVariantList adapterItems READ adapterItems NOTIFY changed)
    Q_PROPERTY(int selectedAdapter READ selectedAdapter WRITE setSelectedAdapter NOTIFY changed)
    Q_PROPERTY(QStringList logs READ logs NOTIFY changed)
    Q_PROPERTY(QString savedUser READ savedUser NOTIFY credentialsChanged)
    Q_PROPERTY(QString savedPassword READ savedPassword NOTIFY credentialsChanged)
    Q_PROPERTY(bool remembered READ remembered NOTIFY credentialsChanged)
    Q_PROPERTY(bool autoLogin READ autoLogin WRITE setAutoLogin NOTIFY changed)
    Q_PROPERTY(bool startWithWindows READ startWithWindows WRITE setStartWithWindows NOTIFY changed)
    Q_PROPERTY(qulonglong heartbeats READ heartbeats NOTIFY changed)
    Q_PROPERTY(QString uptime READ uptime NOTIFY changed)
    Q_PROPERTY(QString heartbeatAge READ heartbeatAge NOTIFY changed)
    Q_PROPERTY(QString bindingLabel READ bindingLabel NOTIFY changed)
    Q_PROPERTY(QString routeName READ routeName NOTIFY changed)
    Q_PROPERTY(QString internetStatus READ internetStatus NOTIFY changed)
    Q_PROPERTY(bool checkingInternet READ checkingInternet NOTIFY changed)
    Q_PROPERTY(QString internetOutcome READ internetOutcome NOTIFY changed)
    Q_PROPERTY(QString internetLatency READ internetLatency NOTIFY changed)
    Q_PROPERTY(QString downloadRate READ downloadRate NOTIFY changed)
    Q_PROPERTY(QString uploadRate READ uploadRate NOTIFY changed)
    Q_PROPERTY(QString downloadTotal READ downloadTotal NOTIFY changed)
    Q_PROPERTY(QString uploadTotal READ uploadTotal NOTIFY changed)
    Q_PROPERTY(QString trafficTotal READ trafficTotal NOTIFY changed)
    Q_PROPERTY(QVariantList trafficHistory READ trafficHistory NOTIFY changed)
    Q_PROPERTY(bool trafficAvailable READ trafficAvailable NOTIFY changed)
    Q_PROPERTY(bool autoReconnect READ autoReconnect WRITE setAutoReconnect NOTIFY changed)
    Q_PROPERTY(bool closeToTray READ closeToTray WRITE setCloseToTray NOTIFY changed)
    Q_PROPERTY(bool notifications READ notifications WRITE setNotifications NOTIFY changed)
    Q_PROPERTY(bool periodicCheck READ periodicCheck WRITE setPeriodicCheck NOTIFY changed)
    Q_PROPERTY(QString probeUrl READ probeUrl WRITE setProbeUrl NOTIFY changed)
    Q_PROPERTY(int retrySeconds READ retrySeconds NOTIFY changed)
    Q_PROPERTY(QStringList recentLogs READ recentLogs NOTIFY changed)
public:
    explicit Controller(bool demo, QObject* parent = nullptr, const QString& settingsFile = {});
    ~Controller() override;
    QString status() const;
    QString detail() const;
    bool online() const { return session_.verified() && active(); }
    bool active() const { return session_.wantsConnection(); }
    bool busy() const { return active() || quitRequested_ || session_.state() == jlu::State::LogoutChallenge || session_.state() == jlu::State::Logout; }
    bool demo() const { return demo_; }
    QString adapterLabel() const;
    QString ip() const;
    QString routeLabel() const;
    bool routeConflict() const;
    QVariantList adapterItems() const;
    int selectedAdapter() const { return selected_; }
    void setSelectedAdapter(int index);
    QStringList logs() const { return logs_; }
    QString savedUser() const { return savedUser_; }
    QString savedPassword() const { return savedPassword_; }
    bool remembered() const { return remembered_; }
    bool autoLogin() const { return autoLogin_; }
    void setAutoLogin(bool enabled);
    bool startWithWindows() const;
    void setStartWithWindows(bool enabled);
    qulonglong heartbeats() const { return session_.heartbeatCount(); }
    QString uptime() const;
    QString heartbeatAge() const;
    QString bindingLabel() const;
    QString routeName() const;
    QString internetStatus() const { return internetStatus_; }
    bool checkingInternet() const { return !probeReply_.isNull(); }
    QString internetOutcome() const { return internetOutcome_; }
    QString internetLatency() const { return internetElapsed_ < 0 ? QStringLiteral("—") : QStringLiteral("%1 ms").arg(internetElapsed_); }
    QString downloadRate() const;
    QString uploadRate() const;
    QString downloadTotal() const;
    QString uploadTotal() const;
    QString trafficTotal() const;
    QVariantList trafficHistory() const { return trafficHistory_; }
    bool trafficAvailable() const { return traffic_.available(); }
    bool autoReconnect() const { return autoReconnect_; }
    bool closeToTray() const { return closeToTray_; }
    bool notifications() const { return notifications_; }
    bool periodicCheck() const { return periodicCheck_; }
    QString probeUrl() const { return probeUrl_; }
    int retrySeconds() const;
    QStringList recentLogs() const { return logs_.mid(0, 3); }
    void setAutoReconnect(bool enabled);
    void setCloseToTray(bool enabled);
    void setNotifications(bool enabled);
    void setPeriodicCheck(bool enabled);
    void setProbeUrl(const QString& url);
    Q_INVOKABLE void connectNow(QString user, QString password, bool remember);
    Q_INVOKABLE quint32 interfaceIndex() const { const auto host = selectedHost(); return host ? host->interfaceIndex : 0; }
    Q_INVOKABLE void disconnectNow();
    Q_INVOKABLE void simulateDrop();
    Q_INVOKABLE void simulateResume();
    Q_INVOKABLE void refreshNetwork();
    Q_INVOKABLE void exportLogs(const QString& dialogTitle);
    Q_INVOKABLE void clearLogs();
    Q_INVOKABLE void requestQuit();
    Q_INVOKABLE void connectSaved();
    Q_INVOKABLE void retryNow();
    Q_INVOKABLE void clearCredentials();
    Q_INVOKABLE void checkInternet();
    Q_INVOKABLE void diagnose();
    Q_INVOKABLE void copyLogs();
    Q_INVOKABLE void openLicense();
    Q_INVOKABLE QString licenseText() const;
    Q_INVOKABLE void closeWindow() { emit closeWindowRequested(); }
    void reportTrayFailure();
    bool nativeEventFilter(const QByteArray&, void*, qintptr*) override;
signals:
    void changed();
    void readyToQuit();
    void credentialsChanged();
    void showWindowRequested();
    void notificationRequested(QString title, QString message);
    void licenseRequested();
    void closeWindowRequested();
    void errorRequested(QString message);
private:
    std::uint64_t now() const;
    void pump();
    void synchronizeTransport();
    void log(QString text);
    void simulate(const jlu::Bytes& packet);
    void suspend();
    void resume();
    bool savePreference(const QString& key, const QVariant& value);
    void updateNotifications(std::uint64_t time);
    void sampleTraffic(std::uint64_t time);
    std::optional<jlu::Host> selectedHost() const;
    struct Reply { std::uint64_t at; std::uint64_t generation; jlu::Bytes packet; };
    bool demo_, remembered_ = false, dropping_ = false, quitRequested_ = false;
    bool autoLogin_ = true, autoReconnect_ = true, closeToTray_ = true, notifications_ = true, periodicCheck_ = false;
    bool wasOnline_ = false, faultNotified_ = false;
    QString savedUser_, savedPassword_, localError_;
    QString savedAdapterId_, internetStatus_ = QStringLiteral("未检测"), probeUrl_ = QStringLiteral("https://www.gstatic.com/generate_204");
    std::unique_ptr<QSettings> settings_;
    QNetworkAccessManager networkManager_;
    QPointer<QNetworkReply> probeReply_;
    QString internetOutcome_ = QStringLiteral("unknown");
    qint64 internetElapsed_ = -1;
    jlu::TrafficMeter traffic_;
    QVariantList trafficHistory_;
    std::uint64_t lastTraffic_ = 0;
    QElapsedTimer clock_;
    QTimer timer_;
    jlu::Session session_;
    // Normal startup uses live transport; explicit --demo never creates a socket.
    std::unique_ptr<jlu::UdpTransport> transport_;
    std::vector<jlu::Adapter> adapters_;
    jlu::RouteInfo route_;
    std::deque<Reply> replies_;
    QStringList logs_;
    int selected_ = -1;
    std::uint64_t generation_ = 0, lastPump_ = 0, lastNetwork_ = 0, onlineSince_ = 0, displaySecond_ = 0;
    jlu::State lastState_ = jlu::State::Idle;
    std::string lastMessage_;
    std::uint64_t lastHeartbeat_ = 0, previousHeartbeatCount_ = 0, faultSince_ = 0, lastInternetCheck_ = 0;
};
