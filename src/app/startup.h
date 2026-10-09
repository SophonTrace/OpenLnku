#pragma once
#include <QStringList>
#include <algorithm>

enum class StartupMode { Live, Demo, Invalid };

inline StartupMode startupMode(const QStringList& arguments) {
    const auto has = [&](const QString& option) {
        return std::any_of(arguments.begin(), arguments.end(), [&](const auto& arg) { return arg == option || arg.startsWith(option + '='); });
    };
    const bool demo = has("--demo");
    if ((demo && has("--live")) ||
        (!demo && (has("--capture") || has("--demo-connect") || has("--settings-file") || has("--test-identity") ||
                   has("--scale") || has("--quit-after") || has("--verify-tray") || has("--verify-ui") || has("--demo-license") || has("--verify-tray-failure"))))
        return StartupMode::Invalid;
    return demo ? StartupMode::Demo : StartupMode::Live;
}
