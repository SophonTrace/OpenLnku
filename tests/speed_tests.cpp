// Offline SpeedTest regressions. Every test constructs only SpeedTest(true),
// except a live-mode validation case that rejects missing adapter data before
// creating its worker. No test creates a socket or consults system proxy state.
#include "../src/app/speedtest.h"

#include <QAbstractSocket>
#include <QHostAddress>
#include <QSignalSpy>
#include <QVariantMap>
#include <QtTest>

class SpeedTests final : public QObject {
    Q_OBJECT

private slots:
    void demoPreviewIsExplicitlySyntheticAndComplete() {
        SpeedTest speed(true);
        QSignalSpy changed(&speed, &SpeedTest::changed);
        QVERIFY(changed.isValid());
        QVERIFY(!speed.running());
        QCOMPARE(speed.mode(), 0);
        QCOMPARE(speed.phase(), QStringLiteral("准备测速"));
        QCOMPARE(speed.exitIp(), QStringLiteral("—"));

        speed.preview();

        QVERIFY(!speed.running());
        QCOMPARE(speed.phase(), QStringLiteral("演示结果"));
        QCOMPARE(speed.exitIp(), QStringLiteral("192.0.2.86")); // RFC 5737 TEST-NET-1
        QCOMPARE(QHostAddress(speed.exitIp()).protocol(), QAbstractSocket::IPv4Protocol);
        QCOMPARE(speed.node(), QStringLiteral("Cloudflare · HKG"));
        QVERIFY(speed.complete());
        QCOMPARE(speed.download(), 486.2);
        QCOMPARE(speed.upload(), 92.4);
        QCOMPARE(speed.latency(), 24.0);
        QCOMPARE(speed.jitter(), 3.0);
        QCOMPARE(qRound64(speed.dataMiB() * 1048576.0), qint64{186000000});
        QCOMPARE(speed.testedAt(), QStringLiteral("14:32"));
        QCOMPARE(speed.history().size(), 45);
        QVERIFY(changed.size() >= 1);

        const auto first = speed.history().front().toMap();
        const auto last = speed.history().back().toMap();
        QVERIFY(first.value(QStringLiteral("valid")).toBool());
        QVERIFY(last.value(QStringLiteral("valid")).toBool());
        QCOMPARE(first.value(QStringLiteral("seconds")).toDouble(), 0.0);
        QCOMPARE(last.value(QStringLiteral("seconds")).toDouble(), 20.0);
        QVERIFY(first.value(QStringLiteral("down")).toDouble() < last.value(QStringLiteral("down")).toDouble());
        QVERIFY(first.value(QStringLiteral("up")).toDouble() < last.value(QStringLiteral("up")).toDouble());
    }

    void proxyAndPhysicalModesKeepIndependentSnapshots() {
        SpeedTest speed(true);
        speed.setMode(0);
        speed.start(0, QString(), QString()); // demo mode never opens a channel
        QVERIFY(!speed.running());
        QCOMPARE(speed.exitIp(), QStringLiteral("192.0.2.86"));
        const double proxyRate = speed.download();

        speed.setMode(1);
        QVERIFY(!speed.running());
        QCOMPARE(speed.phase(), QStringLiteral("准备测速"));
        QCOMPARE(speed.exitIp(), QStringLiteral("—"));
        speed.start(0, QStringLiteral("not-an-ip"), QStringLiteral("synthetic adapter"));
        QVERIFY(!speed.running());
        QCOMPARE(speed.exitIp(), QStringLiteral("198.51.100.24")); // RFC 5737 TEST-NET-2
        QCOMPARE(speed.download(), proxyRate);

        speed.setMode(0);
        QCOMPARE(speed.exitIp(), QStringLiteral("192.0.2.86"));
        QCOMPARE(speed.phase(), QStringLiteral("演示结果"));
        QCOMPARE(speed.download(), proxyRate);
        speed.setMode(1);
        QCOMPARE(speed.exitIp(), QStringLiteral("198.51.100.24"));
        QCOMPARE(speed.phase(), QStringLiteral("演示结果"));
    }

    void invalidModeValuesAreIgnored() {
        SpeedTest speed(true);
        QSignalSpy changed(&speed, &SpeedTest::changed);
        QVERIFY(changed.isValid());

        speed.setMode(-1);
        speed.setMode(2);

        QCOMPARE(speed.mode(), 0);
        QCOMPARE(changed.size(), 0);
        speed.setMode(1);
        QCOMPARE(speed.mode(), 1);
        QCOMPARE(changed.size(), 1);
        speed.setMode(99);
        QCOMPARE(speed.mode(), 1);
        QCOMPARE(changed.size(), 1);
    }

    void demoCancelAndStartNeverCreateAWorker() {
        SpeedTest speed(true);
        speed.cancel();
        QVERIFY(!speed.running());

        // Deliberately invalid physical parameters are ignored in demo mode;
        // start() must return through preview() without constructing Channel.
        speed.setMode(1);
        speed.start(0, QStringLiteral("invalid"), QStringLiteral("fake"));

        QVERIFY(!speed.running());
        QCOMPARE(speed.phase(), QStringLiteral("演示结果"));
        QCOMPARE(speed.exitIp(), QStringLiteral("198.51.100.24"));
        QVERIFY(speed.complete());
    }

    void livePhysicalModeRejectsMissingAdapterBeforeWorkerStarts() {
        SpeedTest speed(false);
        speed.setMode(1);
        QSignalSpy changed(&speed, &SpeedTest::changed);
        QVERIFY(changed.isValid());

        // This validation returns before WSAStartup/WSASocket; it needs no
        // route, socket, proxy lookup, credentials, or external connection.
        speed.start(0, QStringLiteral("not-an-ip"), QStringLiteral(""));

        QVERIFY(!speed.running());
        QCOMPARE(speed.phase(), QStringLiteral("无法开始"));
        QCOMPARE(speed.error(), QStringLiteral("请选择具有 IPv4 地址的实体网卡"));
        QCOMPARE(changed.size(), 1);
    }
};

QTEST_GUILESS_MAIN(SpeedTests)
#include "speed_tests.moc"
