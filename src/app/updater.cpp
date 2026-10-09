#include "updater.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QNetworkProxyFactory>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QUrl>
#include <QDateTime>
#include <QSettings>
#include <QSaveFile>
#include <QVersionNumber>
#include <functional>

namespace {
const QString repository = "https://github.com/SophonTrace/OpenNex/releases/download/";
bool releaseAsset(const QString& url) {
    const QUrl parsed(url);
    return parsed.scheme() == "https" && parsed.host() == "github.com" && parsed.userInfo().isEmpty() &&
        parsed.port(-1) == -1 && parsed.query().isEmpty() && parsed.fragment().isEmpty() && url.startsWith(repository);
}
}
Updater::Updater(bool demo, QObject* parent) : QObject(parent), demo_(demo) {
    if (!demo_) {
        const QDir cache(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/Updates");
        for (const auto& directory : cache.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time)) {
            if (directory.isSymLink() || !QRegularExpression("^[0-9a-f-]{36}$").match(directory.fileName()).hasMatch()) continue;
            QFile journal(directory.filePath() + "/journal.json");
            if (!journal.open(QIODevice::ReadOnly) || journal.size() > 1048576) continue;
            const auto state = QJsonDocument::fromJson(journal.readAll()).object();
            if (state.value("state") == "applying" && QDir(state.value("app").toString()).absolutePath() == QCoreApplication::applicationDirPath()) {
                recovery_ = true; jobDir_ = directory.filePath(); status_ = QStringLiteral("上次更新中断，请先恢复旧版"); break;
            }
            QFile result(directory.filePath() + "/result.json");
            if (status_ == QStringLiteral("尚未检查") && result.open(QIODevice::ReadOnly) && result.size() < 65536)
                status_ = QJsonDocument::fromJson(result.readAll()).object().value("message").toString().left(500);
        }
    }
    if (!demo_) QTimer::singleShot(15000, this, [this] {
        if (recovery_) return;
        QSettings settings("OpenNex", "OpenNex");
        const auto last = settings.value("updatesLastChecked").toDateTime();
        if (last.isValid() && last.secsTo(QDateTime::currentDateTimeUtc()) < 86400) return;
        check();
    });
}
Updater::~Updater() { ++generation_; if (reply_) { reply_->disconnect(this); reply_->abort(); } }
void Updater::fail(const QString& message) { status_ = message; ready_ = false; emit changed(); }
void Updater::fetch(const QUrl& url, qsizetype limit, std::function<void(QByteArray)> next) {
    QNetworkRequest request(url); request.setTransferTimeout(15000);
    request.setRawHeader("User-Agent", "OpenNex/1.1"); request.setRawHeader("Accept", "application/vnd.github+json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    auto* response = network_.get(request); reply_ = response; const auto generation = generation_;
    auto data = std::make_shared<QByteArray>();
    connect(response, &QNetworkReply::readyRead, this, [response, data, limit] { *data += response->readAll(); if (data->size() > limit) response->abort(); });
    QTimer::singleShot(25000, response, [response] { if (!response->isFinished()) response->abort(); });
    connect(response, &QNetworkReply::finished, this, [=, this] {
        *data += response->readAll(); response->deleteLater();
        if (generation != generation_) return;
        reply_ = nullptr;
        if (data->size() > limit || response->error() != QNetworkReply::NoError || response->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
            fail(response->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 404 ? QStringLiteral("仓库尚无公开发布版本") : QStringLiteral("检查更新失败，请稍后重试")); return;
        }
        next(*data); emit changed();
    }); emit changed();
}
void Updater::check() {
    if (busy() || recovery_) return;
    if (demo_) { status_ = QStringLiteral("离线演示 · 不访问更新服务器"); emit changed(); return; }
    ++generation_; manifest_ = {}; ready_ = false; progress_ = 0; notes_.clear(); status_ = QStringLiteral("检查新版本");
    QNetworkProxyFactory::setUseSystemConfiguration(true);
    fetch(QUrl("https://api.github.com/repos/SophonTrace/OpenNex/releases/latest"), 1048576, [this](QByteArray bytes) {
        const auto object = QJsonDocument::fromJson(bytes).object();
        const auto tag = object.value("tag_name").toString();
        if (!QRegularExpression("^v[0-9]+\\.[0-9]+\\.[0-9]+$").match(tag).hasMatch() || object.value("draft").toBool() || object.value("prerelease").toBool()) { fail(QStringLiteral("发布版本格式无效")); return; }
        QString jsonUrl, signatureUrl; QHash<QString, QString> assets;
        for (const auto& item : object.value("assets").toArray()) { const auto asset = item.toObject(); assets.insert(asset.value("name").toString(), asset.value("browser_download_url").toString()); }
        jsonUrl = assets.value("update.json"); signatureUrl = assets.value("update.sig");
        const QString expectedBase = repository + tag + "/";
        if (jsonUrl != expectedBase + "update.json" || signatureUrl != expectedBase + "update.sig" || !releaseAsset(jsonUrl)) { fail(QStringLiteral("发布版本缺少签名更新清单")); return; }
        notes_ = object.value("body").toString().left(10000); status_ = QStringLiteral("验证更新签名");
        fetch(QUrl(jsonUrl), 65536, [this, signatureUrl, expectedBase, tag, assets](QByteArray manifestBytes) {
            manifestBytes_ = manifestBytes;
            fetch(QUrl(signatureUrl), 64, [this, expectedBase, tag, assets](QByteArray signature) {
                QFile key(":/updates/packaging/update-public-key.hex"); if (!key.open(QIODevice::ReadOnly)) { fail(QStringLiteral("更新验证密钥不可用")); return; }
                if (!verifyUpdateSignature(manifestBytes_, signature, QByteArray::fromHex(key.readAll().trimmed()))) { fail(QStringLiteral("更新签名无效，已拒绝更新")); return; }
                QString error;
                if (!parseUpdateManifest(manifestBytes_, "0.0.0", manifest_, error)) { manifest_ = {}; fail(error); return; }
                archiveUrl_ = assets.value(manifest_.archive);
                if (tag != "v" + manifest_.version || archiveUrl_ != expectedBase + manifest_.archive || !releaseAsset(archiveUrl_)) { manifest_ = {}; fail(QStringLiteral("更新包与签名版本不一致")); return; }
                QSettings("OpenNex", "OpenNex").setValue("updatesLastChecked", QDateTime::currentDateTimeUtc());
                if (QVersionNumber::compare(QVersionNumber::fromString(manifest_.version), QVersionNumber::fromString(QCoreApplication::applicationVersion())) <= 0) {
                    manifest_ = {}; status_ = QStringLiteral("已是最新版本"); return;
                }
                signature_ = signature; status_ = QStringLiteral("发现新版本 %1").arg(manifest_.version);
            });
        });
    });
}
void Updater::cancel() {
    ++generation_;
    if (reply_) { auto* old = reply_.data(); reply_ = nullptr; old->disconnect(this); old->abort(); old->deleteLater(); }
    download_.reset(); ready_ = false; status_ = QStringLiteral("已取消"); emit changed();
}
void Updater::download() {
    if (demo_ || busy() || recovery_ || !available() || ready_) return;
    jobDir_ = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/Updates/" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!QDir().mkpath(jobDir_)) { fail(QStringLiteral("无法创建更新缓存")); return; }
    download_ = std::make_unique<QFile>(jobDir_ + "/package.zip");
    if (!download_->open(QIODevice::WriteOnly | QIODevice::NewOnly)) { fail(QStringLiteral("无法保存更新包")); return; }
    QNetworkRequest request(QUrl{archiveUrl_}); request.setTransferTimeout(20000);
    request.setRawHeader("User-Agent", "OpenNex/1.1"); request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    auto* response = network_.get(request); reply_ = response; const auto generation = ++generation_;
    auto count = std::make_shared<qint64>(0); auto hash = std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256); auto good = std::make_shared<bool>(true);
    const auto drain = [this, response, count, hash, good, generation] {
        if (generation != generation_) return;
        const auto bytes = response->readAll(); *count += bytes.size();
        if (*count > manifest_.bytes || !download_ || download_->write(bytes) != bytes.size()) { *good = false; response->abort(); return; }
        hash->addData(bytes); progress_ = double(*count) / double(manifest_.bytes); emit changed();
    };
    connect(response, &QNetworkReply::readyRead, this, drain);
    QTimer::singleShot(600000, response, [response] { if (!response->isFinished()) response->abort(); });
    connect(response, &QNetworkReply::finished, this, [=, this] {
        if (generation != generation_) { response->deleteLater(); return; }
        drain(); reply_ = nullptr; response->deleteLater();
        const bool valid = *good && response->error() == QNetworkReply::NoError && response->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200 && *count == manifest_.bytes && hash->result().toHex() == manifest_.sha256.toLatin1();
        if (download_) { if (!download_->flush()) *good = false; download_->close(); }
        download_.reset();
        if (!valid || !*good) { fail(QStringLiteral("更新包下载或校验失败，当前版本保持运行")); return; }
        ready_ = true; status_ = QStringLiteral("更新包已验证，可以重启更新"); emit changed();
    });
    status_ = QStringLiteral("下载更新包"); emit changed();
}
void Updater::install() {
    if (demo_ || busy() || recovery_ || !ready_) return;
    const QJsonObject job{{"parentId", double(QCoreApplication::applicationPid())}, {"appRoot", QCoreApplication::applicationDirPath()},
        {"jobRoot", jobDir_}, {"version", manifest_.version}, {"archive", jobDir_ + "/package.zip"}, {"sha256", manifest_.sha256}, {"bytes", double(manifest_.bytes)}};
    QSaveFile savedJob(jobDir_ + "/job.json");
    const auto jobBytes = QJsonDocument(job).toJson(QJsonDocument::Compact);
    if (!savedJob.open(QIODevice::WriteOnly) || savedJob.write(jobBytes) != jobBytes.size() || !savedJob.commit()) { fail(QStringLiteral("无法保存更新恢复记录")); return; }
    launchHelper(job);
}
void Updater::recover() {
    if (demo_ || busy() || !recovery_) return;
    QFile savedJob(jobDir_ + "/job.json");
    if (!savedJob.open(QIODevice::ReadOnly) || savedJob.size() > 65536) { fail(QStringLiteral("恢复记录缺失，请重新运行安装器")); return; }
    auto job = QJsonDocument::fromJson(savedJob.readAll()).object();
    if (job.value("appRoot").toString() != QCoreApplication::applicationDirPath() || job.value("jobRoot").toString() != jobDir_) { fail(QStringLiteral("恢复记录与当前安装不匹配")); return; }
    job.remove("testOnly"); job.remove("failAfter"); job["parentId"] = double(QCoreApplication::applicationPid());
    launchHelper(job);
}
void Updater::launchHelper(QJsonObject job) {
    QFile helper(":/updates/tools/apply-update.ps1");
    if (!helper.open(QIODevice::ReadOnly)) { fail(QStringLiteral("更新助手不可用")); return; }
    const auto json = QJsonDocument(job).toJson(QJsonDocument::Compact).toBase64();
    const auto helperBytes = helper.readAll();
    const QString helperPath = jobDir_ + "/apply-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".ps1";
    QFile savedHelper(helperPath);
    if (!savedHelper.open(QIODevice::WriteOnly | QIODevice::NewOnly) || savedHelper.write(helperBytes) != helperBytes.size() || !savedHelper.flush()) { fail(QStringLiteral("无法准备更新助手")); return; }
    savedHelper.close();
    const auto path = helperPath.toUtf8().toBase64();
    const auto helperHash = QCryptographicHash::hash(helperBytes, QCryptographicHash::Sha256).toHex();
    const QString script = "$OpenNexJobJson = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('" + QString::fromLatin1(json) + "'))\n"
        "$OpenNexHelperPath = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('" + QString::fromLatin1(path) + "'))\n"
        "if ((Get-FileHash -LiteralPath $OpenNexHelperPath -Algorithm SHA256).Hash -ne '" + QString::fromLatin1(helperHash) + "') { throw 'Update helper integrity mismatch' }\n"
        "& ([ScriptBlock]::Create([IO.File]::ReadAllText($OpenNexHelperPath, [Text.Encoding]::UTF8)))";
    const auto encoded = QByteArray(reinterpret_cast<const char*>(script.utf16()), script.size() * 2).toBase64();
    const QString shell = qEnvironmentVariable("WINDIR") + "/System32/WindowsPowerShell/v1.0/powershell.exe";
    if (!QProcess::startDetached(shell, {"-NoProfile", "-NonInteractive", "-WindowStyle", "Hidden", "-EncodedCommand", QString::fromLatin1(encoded)}, jobDir_)) {
        fail(QStringLiteral("无法启动更新助手")); return;
    }
    ready_ = false;
    status_ = QStringLiteral("正在退出并更新"); emit changed(); emit installationStarted();
}
