#pragma once
#include <QByteArray>
#include <QString>
struct UpdateManifest {
    QString version, archive, sha256;
    qint64 bytes = 0;
};
bool verifyUpdateSignature(const QByteArray& manifest, const QByteArray& signature, const QByteArray& publicKey);
bool parseUpdateManifest(const QByteArray& bytes, const QString& installedVersion, UpdateManifest& result, QString& error);
