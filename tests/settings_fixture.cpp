// Installer-only synthetic settings diagnostics. Never reads or writes production identities.
#include <windows.h>
#include <QCoreApplication>
#include <QDebug>
#include <QSettings>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#pragma comment(lib, "Advapi32.lib")

namespace {
QString syntheticRegistryPath(const QString& identity) {
    return QStringLiteral("Software\\%1\\%1").arg(identity);
}

const char* statusName(QSettings::Status status) {
    switch (status) {
    case QSettings::NoError: return "NoError";
    case QSettings::AccessError: return "AccessError";
    case QSettings::FormatError: return "FormatError";
    }
    return "Unknown";
}

void reportTokenIdentity() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        qInfo().noquote() << "token.openStatus=" << GetLastError();
        return;
    }

    DWORD userSize = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &userSize);
    std::vector<BYTE> userBuffer(userSize);
    if (!userSize || !GetTokenInformation(token, TokenUser, userBuffer.data(), userSize, &userSize)) {
        qInfo().noquote() << "token.userStatus=" << GetLastError();
    } else {
        const auto* user = reinterpret_cast<const TOKEN_USER*>(userBuffer.data());
        const auto* sid = static_cast<const BYTE*>(user->User.Sid);
        const DWORD sidLength = IsValidSid(user->User.Sid) ? GetLengthSid(user->User.Sid) : 0;
        if (!sidLength) {
            qInfo().noquote() << "token.sidStatus=invalid SID";
        } else {
            std::uint64_t hash = 14695981039346656037ull;
            for (DWORD index = 0; index < sidLength; ++index) {
                hash ^= sid[index];
                hash *= 1099511628211ull;
            }
            qInfo().noquote() << "token.sidFNV1a64=" << QString::number(hash, 16).rightJustified(16, QLatin1Char('0'));
        }
    }

    DWORD integritySize = 0;
    GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &integritySize);
    std::vector<BYTE> integrityBuffer(integritySize);
    if (!integritySize || !GetTokenInformation(token, TokenIntegrityLevel,
                                                integrityBuffer.data(), integritySize, &integritySize)) {
        qInfo().noquote() << "token.integrityStatus=" << GetLastError();
    } else {
        const auto* label = reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(integrityBuffer.data());
        const auto* count = GetSidSubAuthorityCount(label->Label.Sid);
        if (!count || *count == 0) {
            qInfo().noquote() << "token.integrityStatus=missing RID";
        } else {
            const auto* rid = GetSidSubAuthority(label->Label.Sid, static_cast<DWORD>(*count - 1));
            qInfo().noquote() << "token.integrityRID=" << (rid ? *rid : 0);
        }
    }
    CloseHandle(token);
}

struct AccessProbe {
    const wchar_t* label;
    REGSAM access;
};

void reportRegistryAccess(const QString& identity) {
    const auto registryPath = syntheticRegistryPath(identity);
    const std::array probes{
        AccessProbe{L"KEY_SET_VALUE", KEY_SET_VALUE},
        AccessProbe{L"QUERY|SET", KEY_QUERY_VALUE | KEY_SET_VALUE},
        AccessProbe{L"KEY_READ|KEY_WRITE", KEY_READ | KEY_WRITE},
        AccessProbe{L"KEY_ALL_ACCESS", KEY_ALL_ACCESS},
    };

    qInfo().noquote() << "Win32.registryPath=" << registryPath;
    for (std::size_t index = 0; index < probes.size(); ++index) {
        HKEY key = nullptr;
        DWORD disposition = 0;
        const LSTATUS createStatus = RegCreateKeyExW(
            HKEY_CURRENT_USER,
            reinterpret_cast<LPCWSTR>(registryPath.utf16()),
            0, nullptr, REG_OPTION_NON_VOLATILE, probes[index].access, nullptr,
            &key, &disposition);
        LSTATUS setStatus = ERROR_INVALID_HANDLE;
        LSTATUS deleteStatus = ERROR_INVALID_HANDLE;
        if (createStatus == ERROR_SUCCESS) {
            const QString valueName = QStringLiteral("__OpenNexProbe_%1_%2")
                                          .arg(GetCurrentProcessId()).arg(index);
            const std::wstring nativeValueName = valueName.toStdWString();
            const DWORD value = static_cast<DWORD>(probes[index].access);
            setStatus = RegSetValueExW(
                key, nativeValueName.c_str(), 0, REG_DWORD,
                reinterpret_cast<const BYTE*>(&value), sizeof(value));
            if (setStatus == ERROR_SUCCESS)
                deleteStatus = RegDeleteValueW(key, nativeValueName.c_str());
            RegCloseKey(key);
        }
        qInfo().noquote()
            << "Win32.access=" << QString::fromWCharArray(probes[index].label)
            << "mask=0x" + QString::number(probes[index].access, 16)
            << "createStatus=" << createStatus
            << "disposition=" << disposition
            << "setStatus=" << setStatus
            << "deleteStatus=" << deleteStatus;
    }
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() != 2 || (args[1] != "legacy" && args[1] != "current")) return 2;

    const QString identity = args[1] == "legacy"
        ? QStringLiteral("OpenNexAcceptanceLegacy")
        : QStringLiteral("OpenNexAcceptance");
    QSettings settings(identity, identity);
    qInfo().noquote() << "fixture.mode=" << args[1]
                      << "QSettings.fileName=" << settings.fileName()
                      << "QSettings.initialStatus=" << static_cast<int>(settings.status())
                      << statusName(settings.status());
    reportTokenIdentity();

    settings.setValue(QStringLiteral("username"), QStringLiteral("fixture-account"));
    settings.setValue(QStringLiteral("credential"), QByteArray("synthetic-encrypted-placeholder"));
    settings.setValue(QStringLiteral("autoLogin"), false);
    settings.setValue(QStringLiteral("autoReconnect"), false);
    settings.setValue(QStringLiteral("startWithWindows"), false);
    settings.sync();
    qInfo().noquote() << "QSettings.afterSyncStatus=" << static_cast<int>(settings.status())
                      << statusName(settings.status());

    reportRegistryAccess(identity);
    qInfo().noquote() << "QSettings.finalStatus=" << static_cast<int>(settings.status())
                      << statusName(settings.status());
    return settings.status() == QSettings::NoError ? 0 : 1;
}
