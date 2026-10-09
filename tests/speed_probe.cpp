// Explicit, single-purpose live-network probe for a human-run acceptance.
// It does not instantiate Controller, run Dr.COM authentication, or modify
// proxy/routes. It asks SpeedTest to report the exit IP and cancels before
// download/upload measurement. It is intentionally opt-in via --network-probe.
#include "../src/app/speedtest.h"
#include "../src/app/proxy_query.h"

#include <QAbstractSocket>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QHostAddress>
#include <QTextStream>
#include <QTimer>
#include <cstdio>
#include <cstdlib>

int main(int argc, char* argv[]) {
    const int proxyQuery = runInternalProxyQuery(argc, argv);
    if (proxyQuery >= 0) return proxyQuery;
    QCoreApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("OpenNex speed probe"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Optional probe that starts one selected SpeedTest mode, prints the first exit IP, then cancels."));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("network-probe"),
                      QStringLiteral("Explicitly allow the probe to contact the configured speed-test endpoints.")});
    parser.addOption({QStringLiteral("mode"), QStringLiteral("Required mode: proxy or direct."), QStringLiteral("mode")});
    parser.addOption({QStringLiteral("interface-index"),
                      QStringLiteral("Required for direct mode: physical interface index."), QStringLiteral("index")});
    parser.addOption({QStringLiteral("source-ip"),
                      QStringLiteral("Required for direct mode: IPv4 assigned to that interface."), QStringLiteral("IPv4")});
    parser.addOption({QStringLiteral("adapter-name"),
                      QStringLiteral("Optional label for the selected interface."), QStringLiteral("name")});
    parser.addOption({QStringLiteral("node"), QStringLiteral("Node: auto, domestic or overseas."), QStringLiteral("node"), QStringLiteral("auto")});
    parser.addOption({QStringLiteral("complete"), QStringLiteral("Explicitly allow the full bounded bandwidth measurement instead of cancelling after IP.")});
    parser.process(application);

    if (!parser.isSet(QStringLiteral("network-probe"))) {
        QTextStream(stderr) << "Refusing network access without --network-probe.\n";
        return 64;
    }
    const QString modeName = parser.value(QStringLiteral("mode"));
    if (modeName != QStringLiteral("proxy") && modeName != QStringLiteral("direct")) {
        QTextStream(stderr) << "Specify --mode proxy or --mode direct.\n";
        return 64;
    }

    quint32 interfaceIndex = 0;
    QString sourceIp;
    QString adapterName;
    if (modeName == QStringLiteral("direct")) {
        bool indexOk = false;
        const auto parsedIndex = parser.value(QStringLiteral("interface-index")).toUInt(&indexOk, 10);
        sourceIp = parser.value(QStringLiteral("source-ip"));
        adapterName = parser.value(QStringLiteral("adapter-name"));
        if (!indexOk || parsedIndex == 0 || QHostAddress(sourceIp).protocol() != QAbstractSocket::IPv4Protocol) {
            QTextStream(stderr) << "Direct mode requires --interface-index N and --source-ip IPv4.\n";
            return 64;
        }
        interfaceIndex = parsedIndex;
    } else if (parser.isSet(QStringLiteral("interface-index")) ||
               parser.isSet(QStringLiteral("source-ip"))) {
        QTextStream(stderr) << "Interface binding options are only valid with --mode direct.\n";
        return 64;
    }

    SpeedTest speed(false);
    speed.setMode(modeName == QStringLiteral("direct") ? 1 : 0);
    const auto nodeName = parser.value(QStringLiteral("node"));
    if (nodeName != "auto" && nodeName != "domestic" && nodeName != "overseas") return 64;
    speed.setNodeChoice(nodeName == "domestic" ? 1 : nodeName == "overseas" ? 2 : 0);
    const bool full = parser.isSet(QStringLiteral("complete"));
    bool started = false;
    bool exitIpFound = false;
    bool watchdogExpired = false;
    int result = 3;
    QString lastPhase;
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, &application, [&] {
        watchdogExpired = true;
        if (speed.running()) {
            speed.cancel();
            // Some platform proxy discovery APIs can block synchronously. Do
            // not let this diagnostic process hang indefinitely on that path.
            QTimer::singleShot(5000, &application, [&] {
                if (speed.running()) std::_Exit(124);
                application.exit(124);
            });
        } else {
            application.exit(124);
        }
    });
    QObject::connect(&speed, &SpeedTest::changed, &application, [&] {
        if (speed.phase() != lastPhase) { lastPhase = speed.phase(); QTextStream(stdout) << "PHASE=" << lastPhase << "\n"; QTextStream(stdout).flush(); }
        if (!exitIpFound) {
            const QHostAddress address(speed.exitIp());
            if (!address.isNull()) {
                exitIpFound = true;
                QTextStream(stdout) << "MODE=" << modeName << "\nEXIT_IP="
                                    << speed.exitIp() << "\n";
                QTextStream(stdout).flush();
                if (!full) speed.cancel();
            }
        }
        if (started && !speed.running()) {
            watchdog.stop();
            if (watchdogExpired) {
                result = 124;
            } else if (full && speed.complete()) {
                QTextStream(stdout) << "NODE=" << speed.node() << "\nDOWN=" << speed.download() << "\nUP=" << speed.upload() << "\nLATENCY=" << speed.latency() << "\nDATA_MIB=" << speed.dataMiB() << "\n";
                result = 0;
            } else if (exitIpFound && speed.phase() == QStringLiteral("已取消")) {
                result = 0;
            } else {
                result = 3;
                QTextStream(stderr) << "Probe ended before a successful cancellation: phase="
                                    << speed.phase() << " error=" << speed.error() << "\n";
                QTextStream(stderr).flush();
            }
            application.exit(result);
        }
    });

    watchdog.start(80000);
    started = true;
    speed.start(interfaceIndex, sourceIp, adapterName);
    if (!speed.running()) {
        watchdog.stop();
        QTextStream(stderr) << "SpeedTest did not start: " << speed.error() << "\n";
        return 3;
    }
    return application.exec();
}
