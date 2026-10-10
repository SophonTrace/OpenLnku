#pragma once
#include <QSettings>

inline void migratePreferences(QSettings& current, const QSettings& legacy) {
    if (current.contains("migrationComplete")) return;
    for (const auto& key : {"username", "credential", "autoLogin", "autoReconnect", "closeToTray", "notifications", "periodicCheck", "probeUrl", "adapterId", "startWithWindows", "startupConfigured", "language"})
        if (!current.contains(key) && legacy.contains(key)) current.setValue(key, legacy.value(key));
    current.setValue("migrationComplete", true);
    current.sync();
}

inline void migrateOpenNexPreferences(QSettings& current, const QSettings& previous) {
    if (current.contains("openNexMigrationComplete")) return;
    for (const auto& key : {"username", "credential", "autoLogin", "autoReconnect", "closeToTray", "notifications", "periodicCheck", "probeUrl", "adapterId", "startWithWindows", "startupConfigured", "language"})
        if (!current.contains(key) && previous.contains(key)) current.setValue(key, previous.value(key));
    current.setValue("openNexMigrationComplete", true); current.sync();
}
