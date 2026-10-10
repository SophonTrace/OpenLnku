#include "app/localization.h"
#include "app/settings_migration.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QSignalSpy>

class LocalizationTests : public QObject {
    Q_OBJECT
private slots:
    void switchPreservesCanonicalState() {
        Localization language(true);
        const auto state = QStringLiteral("认证已通过");
        QCOMPARE(language.translate(state), state);
        language.setLanguage("en");
        QCOMPARE(language.translate(state), QStringLiteral("Authenticated"));
        QCOMPARE(language.translate(QStringLiteral("物理接口 18")), QStringLiteral("Physical interface 18"));
        QCOMPARE(language.translate(QStringLiteral("12:34:56  最近心跳：13 秒前；有效心跳：7。")), QStringLiteral("12:34:56  Last heartbeat: 13 s ago; confirmed heartbeats: 7."));
        QCOMPARE(language.translate(QStringLiteral("TLS 连接失败：192.0.2.44")), QStringLiteral("TLS connection failed: 192.0.2.44"));
        language.setLanguage("zh-CN");
        QCOMPARE(language.translate(state), state);
    }
    void languagePersistsAndInvalidValuesDoNotOverwrite() {
        QTemporaryDir directory(QCoreApplication::applicationDirPath() + "/../.deps/localization-XXXXXX"); QVERIFY(directory.isValid());
        const auto file = directory.filePath("preferences.ini");
        { Localization language(true,file); language.setLanguage("en"); language.setLanguage("invalid"); QCOMPARE(language.language(), QStringLiteral("en")); }
        Localization restored(true,file); QCOMPARE(restored.language(), QStringLiteral("en"));
    }
    void failedSaveKeepsActiveLanguage() {
        QTemporaryDir directory(QCoreApplication::applicationDirPath() + "/../.deps/localization-XXXXXX"); QVERIFY(directory.isValid());
        Localization language(true,directory.path());
        QSignalSpy errors(&language,&Localization::errorRequested);
        language.setLanguage("en");
        QCOMPARE(language.language(),QStringLiteral("zh-CN")); QCOMPARE(errors.count(),1);
    }
    void rebrandingPreservesCredentialAndExplicitChoices() {
        QTemporaryDir directory(QCoreApplication::applicationDirPath() + "/../.deps/localization-XXXXXX"); QVERIFY(directory.isValid());
        QSettings previous(directory.filePath("previous.ini"),QSettings::IniFormat);
        QSettings current(directory.filePath("current.ini"),QSettings::IniFormat);
        const QByteArray encrypted("encrypted-by-current-user");
        previous.setValue("credential",encrypted); previous.setValue("username","example001");
        for (const auto* key : {"autoLogin","autoReconnect","startWithWindows"}) previous.setValue(key,false);
        previous.setValue("language","en"); previous.setValue("closeToTray",false);
        current.setValue("autoReconnect",true);
        migrateOpenNexPreferences(current,previous);
        QCOMPARE(current.value("credential").toByteArray(),encrypted);
        QVERIFY(!current.value("autoLogin").toBool()); QVERIFY(!current.value("startWithWindows").toBool());
        QVERIFY(current.value("autoReconnect").toBool()); QVERIFY(!current.value("closeToTray").toBool());
        QCOMPARE(current.value("language").toString(),QStringLiteral("en"));
        previous.setValue("username","different"); migrateOpenNexPreferences(current,previous);
        QCOMPARE(current.value("username").toString(),QStringLiteral("example001"));
    }
};
QTEST_GUILESS_MAIN(LocalizationTests)
#include "localization_tests.moc"
