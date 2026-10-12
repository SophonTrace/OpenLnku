#include "update_handoff.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QFile>
#include <QJsonDocument>

class UpdateHandoffTests : public QObject {
    Q_OBJECT
    static QJsonObject job(const QTemporaryDir& directory) { return {{"jobRoot", directory.path()}, {"appRoot", directory.path()}}; }
private slots:
    void slowPreparationKeepsParentAliveAndRejectsStaleAcknowledgement() {
        QTemporaryDir directory(QCoreApplication::applicationDirPath() + "/../.deps/handoff 空格-XXXXXX"); QVERIFY(directory.isValid());
        UpdateHandoff handoff; QSignalSpy ready(&handoff, &UpdateHandoff::ready), failed(&handoff, &UpdateHandoff::failed);
        const QByteArray helper = R"PS(
$j = $OpenLnkuJobJson | ConvertFrom-Json
$p = Join-Path $j.jobRoot ('ready-' + $j.launchToken + '.json')
[IO.File]::WriteAllText($p,'{"status":"ready","launchToken":"stale"}')
Start-Sleep -Milliseconds 800
if (-not [Diagnostics.Process]::GetProcessById($j.parentId)) { throw 'Parent exited prematurely' }
[IO.File]::WriteAllText($p,(@{status='ready';launchToken=$j.launchToken}|ConvertTo-Json -Compress))
)PS";
        auto input = job(directory); input["parentId"] = double(QCoreApplication::applicationPid());
        handoff.start(input, helper);
        QVERIFY(handoff.pending()); QTest::qWait(500); QCOMPARE(ready.count(), 0); QCOMPARE(failed.count(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 12000); QVERIFY(!handoff.pending()); QCOMPARE(failed.count(), 0);
    }
    void bootstrapErrorIsVisibleWithoutExitingParent() {
        QTemporaryDir directory(QCoreApplication::applicationDirPath() + "/../.deps/handoff-XXXXXX"); QVERIFY(directory.isValid());
        UpdateHandoff handoff; QSignalSpy ready(&handoff, &UpdateHandoff::ready), failed(&handoff, &UpdateHandoff::failed);
        handoff.start(job(directory), "throw 'Acceptance startup failure'");
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 12000); QCOMPARE(ready.count(), 0); QVERIFY(!handoff.pending());
        QVERIFY(failed.front().front().toString().contains("Acceptance startup failure"));
    }
    void timeoutCancelsLateHelper() {
        QTemporaryDir directory(QCoreApplication::applicationDirPath() + "/../.deps/handoff-XXXXXX"); QVERIFY(directory.isValid());
        UpdateHandoff handoff; QSignalSpy ready(&handoff, &UpdateHandoff::ready), failed(&handoff, &UpdateHandoff::failed);
        const QByteArray helper = R"PS(
$j = $OpenLnkuJobJson | ConvertFrom-Json
Start-Sleep -Milliseconds 600
[IO.File]::WriteAllText((Join-Path $j.jobRoot 'saw-cancellation'),[string][IO.File]::Exists((Join-Path $j.jobRoot ('cancel-' + $j.launchToken))))
)PS";
        handoff.start(job(directory), helper, 200);
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000); QCOMPARE(ready.count(), 0);
        const QString evidence = directory.filePath("saw-cancellation");
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(evidence), 12000); QFile file(evidence); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("True"));
    }
};
QTEST_GUILESS_MAIN(UpdateHandoffTests)
#include "update_handoff_tests.moc"
