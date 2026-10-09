#pragma once
#include <QSettings>

inline void migratePreferences(QSettings& current, const QSettings& legacy) {
    if (current.contains("migrationComplete")) return;
    for (const auto& key : {"username", "credential", "autoLogin", "autoReconnect", "closeToTray", "notifications", "periodicCheck", "probeUrl", "adapterId", "startWithWindows"})
        if (!current.contains(key) && legacy.contains(key)) current.setValue(key, legacy.value(key));
    current.setValue("migrationComplete", true);
    current.sync();
}
