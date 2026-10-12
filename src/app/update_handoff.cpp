#include "update_handoff.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QUuid>

namespace {
QJsonObject readObject(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
}
UpdateHandoff::UpdateHandoff(QObject* parent) : QObject(parent) {
    timer_.setInterval(100);
    connect(&timer_, &QTimer::timeout, this, &UpdateHandoff::poll);
}
void UpdateHandoff::start(QJsonObject job, const QByteArray& helper, int timeoutMs) {
    if (pending_) return;
    pending_ = true;
    timeoutMs_ = timeoutMs;
    directory_ = job.value("jobRoot").toString();
    token_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    job["launchToken"] = token_;
    const QString helperPath = directory_ + "/apply-" + token_ + ".ps1";
    QSaveFile file(helperPath);
    if (helper.isEmpty() || !file.open(QIODevice::WriteOnly) || file.write(helper) != helper.size() || !file.commit()) {
        fail(QStringLiteral("无法准备更新助手，当前版本保持运行")); return;
    }
    const auto json = QJsonDocument(job).toJson(QJsonDocument::Compact).toBase64();
    const auto path = helperPath.toUtf8().toBase64();
    const auto hash = QCryptographicHash::hash(helper, QCryptographicHash::Sha256).toHex();
    // Bootstrap errors must also reach the client; PowerShell's hidden window
    // otherwise conceals errors before the embedded helper is even invoked.
    const QString script = "$ErrorActionPreference = 'Stop'\n"
        "$OpenLnkuJobJson = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('" + QString::fromLatin1(json) + "'))\n"
        "$OpenLnkuHelperPath = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('" + QString::fromLatin1(path) + "'))\n"
        "try {\n"
        "if ((Get-FileHash -LiteralPath $OpenLnkuHelperPath -Algorithm SHA256).Hash -ne '" + QString::fromLatin1(hash) + "') { throw 'Update helper integrity mismatch' }\n"
        "& ([ScriptBlock]::Create([IO.File]::ReadAllText($OpenLnkuHelperPath, [Text.Encoding]::UTF8)))\n"
        "} catch {\n"
        "$j = $OpenLnkuJobJson | ConvertFrom-Json\n"
        "[IO.File]::WriteAllText((Join-Path $j.jobRoot ('result-' + $j.launchToken + '.json')), "
        "(@{status='failed';message=$_.Exception.Message;launchToken=$j.launchToken;app=$j.appRoot}|ConvertTo-Json -Compress), [Text.UTF8Encoding]::new($false))\n"
        "exit 1\n}";
    const auto encoded = QByteArray(reinterpret_cast<const char*>(script.utf16()), script.size() * 2).toBase64();
    QProcess process;
    const QString powershell = qEnvironmentVariable("WINDIR") + "/System32/WindowsPowerShell/v1.0";
    process.setProgram(powershell + "/powershell.exe");
    auto environment = QProcessEnvironment::systemEnvironment();
    // PowerShell 7 can leave a Core-only PSModulePath in the environment.
    // The Windows 5.1 helper must use its own bundled Utility/Security modules.
    environment.insert("PSModulePath", powershell + "/Modules");
    process.setProcessEnvironment(environment);
    process.setArguments({"-NoProfile", "-NonInteractive", "-WindowStyle", "Hidden", "-EncodedCommand", QString::fromLatin1(encoded)});
    process.setWorkingDirectory(directory_);
    process.setStandardOutputFile(directory_ + "/helper-" + token_ + ".stdout.log");
    process.setStandardErrorFile(directory_ + "/helper-" + token_ + ".stderr.log");
    if (!process.startDetached()) { fail(QStringLiteral("无法启动更新助手，当前版本保持运行")); return; }
    elapsed_.start(); timer_.start();
}
void UpdateHandoff::poll() {
    const auto result = readObject(directory_ + "/result-" + token_ + ".json");
    if (result.value("launchToken") == token_) {
        fail(QStringLiteral("更新助手准备失败，当前版本保持运行：%1").arg(result.value("message").toString().left(500))); return;
    }
    const auto acknowledgement = readObject(directory_ + "/ready-" + token_ + ".json");
    if (acknowledgement.value("launchToken") == token_ && acknowledgement.value("status") == "ready") {
        timer_.stop(); pending_ = false; emit ready(); return;
    }
    if (elapsed_.elapsed() >= timeoutMs_) fail(QStringLiteral("更新助手准备超时，当前版本保持运行。请重试或使用安装包更新。"));
}
void UpdateHandoff::fail(const QString& message) {
    timer_.stop(); pending_ = false;
    // A slow helper must never apply a timed-out job after a later manual exit.
    if (!directory_.isEmpty() && !token_.isEmpty()) {
        QFile cancel(directory_ + "/cancel-" + token_);
        if (cancel.open(QIODevice::WriteOnly)) cancel.close();
    }
    emit failed(message);
}
