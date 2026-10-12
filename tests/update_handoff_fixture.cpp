// Harmless executable used only in isolated updater acceptance directories.
// It contains no authentication, settings, network or production instance code.
#include "update_handoff.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTimer>
#include <QDir>
#include <windows.h>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QString root = QCoreApplication::applicationDirPath();
    if (!QFile::exists(root + "/.update-acceptance-owned")) return 90;
    QFile jobFile(root + "/fixture-job.json");
    if (!jobFile.open(QIODevice::ReadOnly)) return 91;
    auto job = QJsonDocument::fromJson(jobFile.readAll()).object();
    if (QDir(job.value("appRoot").toString()).absolutePath() != root) return 92;
    QFile restarted(root + "/restarted.json");
    if (QFile::exists(job.value("jobRoot").toString() + "/result.json")) {
        if (!restarted.open(QIODevice::WriteOnly)) return 93;
        restarted.write(QJsonDocument(QJsonObject{{"version", OPENLNKU_VERSION}, {"pid", double(QCoreApplication::applicationPid())}}).toJson());
        return 0;
    }
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\OpenLnkuAcceptanceUpdate.App.Live");
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) return 94;
    job["parentId"] = double(QCoreApplication::applicationPid());
    QFile helper(root + "/fixture-helper.ps1");
    if (!helper.open(QIODevice::ReadOnly)) { CloseHandle(mutex); return 95; }
    UpdateHandoff handoff;
    QObject::connect(&handoff, &UpdateHandoff::ready, &app, [&] {
        QFile evidence(root + "/parent-acknowledged"); evidence.open(QIODevice::WriteOnly); evidence.write("ready"); evidence.close();
        QTimer::singleShot(job.value("testPauseAfterReadyMs").toInt(0), &app, &QCoreApplication::quit);
    });
    QObject::connect(&handoff, &UpdateHandoff::failed, &app, [&](const QString& message) {
        QFile evidence(root + "/parent-failure"); evidence.open(QIODevice::WriteOnly); evidence.write(message.toUtf8()); evidence.close(); app.exit(96);
    });
    QTimer::singleShot(25000, &app, [&] { app.exit(97); });
    handoff.start(job, helper.readAll());
    const int result = app.exec(); CloseHandle(mutex); return result;
}
