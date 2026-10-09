// Offline tests for the signed update manifest. Keys are generated in memory;
// no release or private signing key is read from disk.
#include "../src/app/update_manifest.h"

#include <windows.h>
#include <bcrypt.h>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QtTest>

namespace {
bool success(NTSTATUS status) { return status >= 0; }

struct TestKey {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    QByteArray publicBlob;

    bool generate() {
        if (!success(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0))) return false;
        if (!success(BCryptGenerateKeyPair(algorithm, &key, 256, 0)) ||
            !success(BCryptFinalizeKeyPair(key, 0))) return false;
        // P-256 public blobs contain the 8-byte BCRYPT_ECCKEY_BLOB header
        // followed by 32-byte X and Y coordinates. Avoid treating the
        // expected STATUS_BUFFER_TOO_SMALL size-query result as a failure.
        ULONG length = sizeof(BCRYPT_ECCKEY_BLOB) + 64;
        publicBlob.resize(static_cast<qsizetype>(length));
        return success(BCryptExportKey(key, nullptr, BCRYPT_ECCPUBLIC_BLOB,
            reinterpret_cast<PUCHAR>(publicBlob.data()), length, &length, 0));
    }

    QByteArray sign(const QByteArray& message) const {
        const auto digest = QCryptographicHash::hash(message, QCryptographicHash::Sha256);
        QByteArray signature(64, Qt::Uninitialized);
        ULONG written = 0;
        if (!success(BCryptSignHash(key, nullptr,
                reinterpret_cast<PUCHAR>(const_cast<char*>(digest.constData())),
                static_cast<ULONG>(digest.size()),
                reinterpret_cast<PUCHAR>(signature.data()),
                static_cast<ULONG>(signature.size()), &written, 0))) return {};
        signature.resize(static_cast<qsizetype>(written));
        return signature;
    }

    void clear() {
        if (key) BCryptDestroyKey(key);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        key = nullptr; algorithm = nullptr;
        publicBlob.fill('\0'); publicBlob.clear();
    }
};

QByteArray manifestBytes(const QString& version = QStringLiteral("1.2.0"),
                         const QString& platform = QStringLiteral("windows-x64"),
                         const QString& archive = QStringLiteral("OpenNex-1.2.0-windows-x64.zip"),
                         const QString& sha256 = QString(64, QLatin1Char('a')),
                         const QJsonValue& bytes = QJsonValue(12345),
                         const QJsonValue& schema = QJsonValue(1)) {
    QJsonObject object{{QStringLiteral("schema"), schema},
                       {QStringLiteral("version"), version},
                       {QStringLiteral("platform"), platform},
                       {QStringLiteral("archive"), archive},
                       {QStringLiteral("bytes"), bytes},
                       {QStringLiteral("sha256"), sha256}};
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}
}

class UpdateTests final : public QObject {
    Q_OBJECT
private:
    TestKey signingKey_;
    TestKey unrelatedKey_;

private slots:
    void initTestCase() {
        QVERIFY(signingKey_.generate());
        QVERIFY(unrelatedKey_.generate());
        QCOMPARE(signingKey_.publicBlob.size(), 72);
        QCOMPARE(unrelatedKey_.publicBlob.size(), 72);
    }

    void cleanupTestCase() {
        signingKey_.clear();
        unrelatedKey_.clear();
    }

    void verifiesExactManifestBytesAndRejectsTampering() {
        const QByteArray bytes = manifestBytes();
        const QByteArray signature = signingKey_.sign(bytes);
        QCOMPARE(signature.size(), 64);
        QVERIFY(verifyUpdateSignature(bytes, signature, signingKey_.publicBlob));

        QByteArray modified = bytes;
        modified.append('\n');
        QVERIFY(!verifyUpdateSignature(modified, signature, signingKey_.publicBlob));

        QByteArray modifiedSignature = signature;
        modifiedSignature[0] = static_cast<char>(modifiedSignature[0] ^ 0x01);
        QVERIFY(!verifyUpdateSignature(bytes, modifiedSignature, signingKey_.publicBlob));
        QVERIFY(!verifyUpdateSignature(bytes, signature.left(63), signingKey_.publicBlob));
        QVERIFY(!verifyUpdateSignature(bytes, signature + '\0', signingKey_.publicBlob));
        QVERIFY(!verifyUpdateSignature(bytes, signature, signingKey_.publicBlob.left(71)));
        QVERIFY(!verifyUpdateSignature(bytes, signature, signingKey_.publicBlob + '\0'));
        QVERIFY(!verifyUpdateSignature(bytes, signature, unrelatedKey_.publicBlob));
        QVERIFY(!verifyUpdateSignature({}, signature, signingKey_.publicBlob));
        QVERIFY(!verifyUpdateSignature(QByteArray(65537, 'x'), signature, signingKey_.publicBlob));
    }

    void parsesNewerWindowsX64Manifest() {
        UpdateManifest result;
        QString error;
        const QByteArray bytes = manifestBytes();
        QVERIFY2(parseUpdateManifest(bytes, QStringLiteral("1.1.0"), result, error), qPrintable(error));
        QCOMPARE(result.version, QStringLiteral("1.2.0"));
        QCOMPARE(result.archive, QStringLiteral("OpenNex-1.2.0-windows-x64.zip"));
        QCOMPARE(result.sha256, QString(64, QLatin1Char('a')));
        QCOMPARE(result.bytes, qint64{12345});
    }

    void rejectsMalformedOrUnsupportedManifest_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<QString>("installedVersion");
        QTest::newRow("invalid-json") << QByteArray("{bad") << QStringLiteral("1.1.0");
        QTest::newRow("wrong-schema") << manifestBytes(QStringLiteral("1.2.0"), QStringLiteral("windows-x64"), QStringLiteral("OpenNex-1.2.0-windows-x64.zip"), QString(64, QLatin1Char('a')), QJsonValue(12345), QJsonValue(2)) << QStringLiteral("1.1.0");
        QTest::newRow("wrong-platform") << manifestBytes(QStringLiteral("1.2.0"), QStringLiteral("windows-arm64")) << QStringLiteral("1.1.0");
        QTest::newRow("bad-version") << manifestBytes(QStringLiteral("v1.2.0")) << QStringLiteral("1.1.0");
        QTest::newRow("bad-installed-version") << manifestBytes() << QStringLiteral("1.1");
        QTest::newRow("same-version") << manifestBytes(QStringLiteral("1.1.0"), QStringLiteral("windows-x64"), QStringLiteral("OpenNex-1.1.0-windows-x64.zip")) << QStringLiteral("1.1.0");
        QTest::newRow("downgrade") << manifestBytes(QStringLiteral("1.0.9"), QStringLiteral("windows-x64"), QStringLiteral("OpenNex-1.0.9-windows-x64.zip")) << QStringLiteral("1.1.0");
        QTest::newRow("wrong-archive") << manifestBytes(QStringLiteral("1.2.0"), QStringLiteral("windows-x64"), QStringLiteral("../OpenNex.zip")) << QStringLiteral("1.1.0");
        QTest::newRow("uppercase-hash") << manifestBytes(QStringLiteral("1.2.0"), QStringLiteral("windows-x64"), QStringLiteral("OpenNex-1.2.0-windows-x64.zip"), QString(64, QLatin1Char('A'))) << QStringLiteral("1.1.0");
        QTest::newRow("short-hash") << manifestBytes(QStringLiteral("1.2.0"), QStringLiteral("windows-x64"), QStringLiteral("OpenNex-1.2.0-windows-x64.zip"), QString(63, QLatin1Char('a'))) << QStringLiteral("1.1.0");
        QTest::newRow("zero-bytes") << manifestBytes(QStringLiteral("1.2.0"), QStringLiteral("windows-x64"), QStringLiteral("OpenNex-1.2.0-windows-x64.zip"), QString(64, QLatin1Char('a')), QJsonValue(0)) << QStringLiteral("1.1.0");
        QTest::newRow("fractional-bytes") << manifestBytes(QStringLiteral("1.2.0"), QStringLiteral("windows-x64"), QStringLiteral("OpenNex-1.2.0-windows-x64.zip"), QString(64, QLatin1Char('a')), QJsonValue(12.5)) << QStringLiteral("1.1.0");
        QTest::newRow("too-large") << manifestBytes(QStringLiteral("1.2.0"), QStringLiteral("windows-x64"), QStringLiteral("OpenNex-1.2.0-windows-x64.zip"), QString(64, QLatin1Char('a')), QJsonValue(268435457)) << QStringLiteral("1.1.0");
        QTest::newRow("oversized-json") << QByteArray(65537, ' ') << QStringLiteral("1.1.0");
    }

    void rejectsMalformedOrUnsupportedManifest() {
        QFETCH(QByteArray, bytes);
        QFETCH(QString, installedVersion);
        UpdateManifest result;
        QString error;
        QVERIFY(!parseUpdateManifest(bytes, installedVersion, result, error));
        QVERIFY(!error.isEmpty());
    }
};

QTEST_APPLESS_MAIN(UpdateTests)
#include "update_tests.moc"
