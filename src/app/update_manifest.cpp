#include "update_manifest.h"
#include <windows.h>
#include <bcrypt.h>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QVersionNumber>
#include <cmath>
bool verifyUpdateSignature(const QByteArray& manifest, const QByteArray& signature, const QByteArray& publicKey) {
    if (manifest.isEmpty() || manifest.size() > 65536 || signature.size() != 64 || publicKey.size() != 72) return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_KEY_HANDLE key = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) < 0) return false;
    bool valid = false;
    if (BCryptImportKeyPair(algorithm, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key,
            reinterpret_cast<PUCHAR>(const_cast<char*>(publicKey.constData())), ULONG(publicKey.size()), 0) >= 0) {
        auto hash = QCryptographicHash::hash(manifest, QCryptographicHash::Sha256);
        valid = BCryptVerifySignature(key, nullptr, reinterpret_cast<PUCHAR>(hash.data()), ULONG(hash.size()),
            reinterpret_cast<PUCHAR>(const_cast<char*>(signature.constData())), ULONG(signature.size()), 0) >= 0;
        BCryptDestroyKey(key);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0); return valid;
}
bool parseUpdateManifest(const QByteArray& bytes, const QString& installedVersion, UpdateManifest& result, QString& error) {
    QJsonParseError parse; const auto doc = QJsonDocument::fromJson(bytes, &parse);
    if (bytes.size() > 65536 || parse.error != QJsonParseError::NoError || !doc.isObject()) { error = QStringLiteral("更新清单格式无效"); return false; }
    const auto object = doc.object(); const QString version = object.value("version").toString();
    const QRegularExpression validVersion("^(0|[1-9][0-9]{0,4})\\.(0|[1-9][0-9]{0,4})\\.(0|[1-9][0-9]{0,4})$");
    if (object.value("schema").toDouble() != 1 || object.value("platform").toString() != "windows-x64" || !validVersion.match(version).hasMatch() || !validVersion.match(installedVersion).hasMatch()) {
        error = QStringLiteral("更新版本或平台不受支持"); return false;
    }
    const auto candidate = QVersionNumber::fromString(version), current = QVersionNumber::fromString(installedVersion);
    if (QVersionNumber::compare(candidate, current) <= 0) { error = QStringLiteral("已是最新版本"); return false; }
    const QString archive = "OpenLnku-" + version + "-windows-x64.zip";
    const QString sha256 = object.value("sha256").toString(); const double length = object.value("bytes").toDouble(-1);
    if (object.value("archive").toString() != archive || !QRegularExpression("^[0-9a-f]{64}$").match(sha256).hasMatch() || !std::isfinite(length) || std::floor(length) != length || length <= 0 || length > 268435456) {
        error = QStringLiteral("更新包名称、大小或校验值无效"); return false;
    }
    result = {version, archive, sha256, qint64(length)}; return true;
}
