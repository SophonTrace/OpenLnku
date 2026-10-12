// Offline SpeedTest regressions. Every test constructs only SpeedTest(true),
// except a live-mode validation case that rejects missing adapter data before
// creating its worker. No test creates a socket or consults system proxy state.
#include "../src/app/speedtest.h"
#include "../src/app/speed_body.h"
#include "../src/app/speed_window.h"
#include "../src/app/proxy_query.h"

#include <QAbstractSocket>
#include <QHostAddress>
#include <QSignalSpy>
#include <QVariantMap>
#include <QtTest>

class SpeedTests final : public QObject {
    Q_OBJECT

private slots:
    void delayedWarmupStillLeavesFullMeasurementWindow() {
        using namespace std::chrono;
        const auto start=SpeedWindow::Time{}; SpeedWindow window(10000,start);
        QVERIFY(!window.begin(start+seconds(2),true));
        QVERIFY(!window.begin(start+seconds(3),false));
        QVERIFY(window.begin(start+seconds(5),true));
        QCOMPARE(window.deadline(),start+seconds(15));
        QCOMPARE(window.measuredMs(start+seconds(15)),std::int64_t(10000));
        QVERIFY(!window.begin(start+seconds(7),true));
        SpeedWindow stalled(7000,start); QVERIFY(!stalled.begin(start+seconds(9),true));
    }
    void protocolSnapshotsRemainIndependent() {
        SpeedTest speed(true); speed.preview(); speed.setProtocolChoice(2);
        QCOMPARE(speed.phase(),QStringLiteral("准备测速")); speed.preview();
        speed.setProtocolChoice(1); QCOMPARE(speed.phase(),QStringLiteral("准备测速"));
        speed.setProtocolChoice(2); QVERIFY(speed.complete());
        speed.setProtocolChoice(-1); speed.setProtocolChoice(3); QCOMPARE(speed.protocolChoice(),2);
        speed.setExtended(true); QVERIFY(speed.extended());
    }

    void exhaustedDiscoveryBudgetAndCancellationRejectBeforeChildLaunch() {
        std::atomic_bool cancelled = false;
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, speedSystemProxy("speed.cloudflare.com", cancelled, 443, 0));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, speedPhysicalDns("speed.cloudflare.com", 1, cancelled, 0));
        cancelled = true;
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, speedSystemProxy("speed.cloudflare.com", cancelled));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, speedPhysicalDns("speed.cloudflare.com", 1, cancelled));
    }
    void streamedChunkCountsBytesBeforeChunkCompletes() {
        SpeedBody body(true, -1, 1048576);
        QCOMPARE(body.feed("100000\r\n" + QByteArray(65536, 'a')).size(), 65536);
        QCOMPARE(body.received(), qint64(65536));
        QVERIFY(!body.complete());
        QCOMPARE(body.feed(QByteArray(1048576 - 65536, 'b')).size(), 1048576 - 65536);
        QCOMPARE(body.feed("\r\n0\r\nX-Test: yes\r\n\r\n").size(), 0);
        QVERIFY(body.complete());
        body.finishEof();
    }
    void splitFramingDoesNotCountHeadersOrTrailers() {
        SpeedBody body(true, -1, 6);
        QCOMPARE(body.feed("3;foo=bar\r").size(), 0);
        QCOMPARE(body.feed("\nabc\r"), QByteArray("abc"));
        QCOMPARE(body.feed("\n3\r\ndef\r\n0\r\n"), QByteArray("def"));
        QVERIFY(!body.complete());
        QCOMPARE(body.feed("\r\n").size(), 0);
        QCOMPARE(body.received(), qint64(6));
        QVERIFY(body.complete());
    }
    void rejectsTruncationOverflowAndInvalidChunkBoundaries() {
        SpeedBody fixed(false, 4, 4);
        QCOMPARE(fixed.feed("abc"), QByteArray("abc"));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, fixed.finishEof());
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, fixed.feed("de"));
        SpeedBody chunked(true, -1, 4);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, chunked.feed("5\r\n"));
        SpeedBody broken(true, -1, 4);
        QCOMPARE(broken.feed("3\r\nabc"), QByteArray("abc"));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, broken.feed("xx"));
        SpeedBody invalid(true, -1, 4);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, invalid.feed("+3\r\n"));
        SpeedBody eof(false, -1, 4);
        QCOMPARE(eof.feed("abc"), QByteArray("abc"));
        eof.finishEof(); QVERIFY(eof.complete());
    }
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

    void nodeChoicesAndModeSlotsRemainIndependent() {
        SpeedTest speed(true);

        // The demo path exercises only the public snapshot-selection API; it
        // never resolves a node or creates a socket.
        QCOMPARE(speed.nodeChoice(), 0); // automatic
        speed.preview();
        QCOMPARE(speed.node(), QStringLiteral("Cloudflare · HKG"));
        QCOMPARE(speed.exitIp(), QStringLiteral("192.0.2.86"));
        const double autoRate = speed.download();

        speed.setNodeChoice(1); // domestic
        QCOMPARE(speed.phase(), QStringLiteral("准备测速"));
        speed.preview();
        QCOMPARE(speed.node(), QStringLiteral("国内 · 上海大学"));
        QCOMPARE(speed.exitIp(), QStringLiteral("192.0.2.86"));
        const double domesticRate = speed.download();

        speed.setNodeChoice(2); // overseas
        QCOMPARE(speed.phase(), QStringLiteral("准备测速"));
        speed.preview();
        QCOMPARE(speed.node(), QStringLiteral("Cloudflare · HKG"));
        QCOMPARE(speed.download(), autoRate);

        // Switching back restores each node-choice snapshot rather than
        // displaying the latest result from another choice.
        speed.setNodeChoice(1);
        QCOMPARE(speed.node(), QStringLiteral("国内 · 上海大学"));
        QCOMPARE(speed.download(), domesticRate);
        speed.setNodeChoice(0);
        QCOMPARE(speed.node(), QStringLiteral("Cloudflare · HKG"));
        QCOMPARE(speed.download(), autoRate);

        // The direct/proxy route dimension is also an independent result slot.
        speed.setMode(1);
        QCOMPARE(speed.nodeChoice(), 0);
        QCOMPARE(speed.phase(), QStringLiteral("准备测速"));
        speed.preview();
        QCOMPARE(speed.exitIp(), QStringLiteral("198.51.100.24"));
        QCOMPARE(speed.download(), autoRate);
        speed.setNodeChoice(1);
        QCOMPARE(speed.phase(), QStringLiteral("准备测速"));
        speed.preview();
        QCOMPARE(speed.node(), QStringLiteral("国内 · 上海大学"));
        QCOMPARE(speed.exitIp(), QStringLiteral("198.51.100.24"));

        speed.setMode(0);
        speed.setNodeChoice(1);
        QCOMPARE(speed.node(), QStringLiteral("国内 · 上海大学"));
        QCOMPARE(speed.exitIp(), QStringLiteral("192.0.2.86"));
        QCOMPARE(speed.download(), domesticRate);
    }

    void invalidNodeChoicesAreIgnored() {
        SpeedTest speed(true);
        QSignalSpy changed(&speed, &SpeedTest::changed);
        QVERIFY(changed.isValid());

        speed.setNodeChoice(-1);
        speed.setNodeChoice(3);
        QCOMPARE(speed.nodeChoice(), 0);
        QCOMPARE(changed.size(), 0);

        speed.setNodeChoice(2);
        QCOMPARE(speed.nodeChoice(), 2);
        QCOMPARE(changed.size(), 1);
        speed.setNodeChoice(99);
        QCOMPARE(speed.nodeChoice(), 2);
        QCOMPARE(changed.size(), 1);
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
