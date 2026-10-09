#pragma once
#include <QObject>
#include <QThread>
#include <QVariantList>
#include <atomic>
#include <array>
#include <memory>

struct SpeedSnapshot {
    QString phase = QStringLiteral("准备测速"), ip = QStringLiteral("—"), node = QStringLiteral("—"), error, time;
    double down = 0, up = 0, latency = -1, jitter = -1;
    quint64 bytes = 0;
    QVariantList samples;
    bool complete = false;
};

class SpeedTest : public QObject {
    Q_OBJECT
    Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY changed)
    Q_PROPERTY(int nodeChoice READ nodeChoice WRITE setNodeChoice NOTIFY changed)
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(QString phase READ phase NOTIFY changed)
    Q_PROPERTY(QString exitIp READ exitIp NOTIFY changed)
    Q_PROPERTY(QString node READ node NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString testedAt READ testedAt NOTIFY changed)
    Q_PROPERTY(double download READ download NOTIFY changed)
    Q_PROPERTY(double upload READ upload NOTIFY changed)
    Q_PROPERTY(double latency READ latency NOTIFY changed)
    Q_PROPERTY(double jitter READ jitter NOTIFY changed)
    Q_PROPERTY(double dataMiB READ dataMiB NOTIFY changed)
    Q_PROPERTY(QVariantList history READ history NOTIFY changed)
    Q_PROPERTY(bool complete READ complete NOTIFY changed)
public:
    explicit SpeedTest(bool demo, QObject* parent = nullptr);
    ~SpeedTest() override;
    int mode() const { return mode_; }
    void setMode(int mode);
    int nodeChoice() const { return nodeChoice_; }
    void setNodeChoice(int choice);
    bool running() const { return worker_ != nullptr; }
    QString phase() const { return current().phase; }
    QString exitIp() const { return current().ip; }
    QString node() const { return current().node; }
    QString error() const { return current().error; }
    QString testedAt() const { return current().time; }
    double download() const { return current().down; }
    double upload() const { return current().up; }
    double latency() const { return current().latency; }
    double jitter() const { return current().jitter; }
    double dataMiB() const { return double(current().bytes) / 1048576.; }
    QVariantList history() const { return current().samples; }
    bool complete() const { return current().complete; }
    Q_INVOKABLE void start(quint32 interfaceIndex, const QString& sourceIp, const QString& adapterName);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void copyExitIp();
    // Synthetic preview is explicit, and never starts a network worker.
    Q_INVOKABLE void preview();
signals:
    void changed();
private:
    size_t slot() const { return size_t(mode_ * 3 + nodeChoice_); }
    const SpeedSnapshot& current() const { return results_[slot()]; }
    bool demo_;
    int mode_ = 0;
    int nodeChoice_ = 0;
    std::array<SpeedSnapshot, 6> results_;
    QThread* worker_ = nullptr;
    std::shared_ptr<std::atomic_bool> cancelled_;
    quint64 generation_ = 0;
};
