#include "localization.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <algorithm>

Localization::Localization(bool demo, const QString& settingsFile, const QString& requested, QObject* parent) : QObject(parent) {
    if (!settingsFile.isEmpty()) settings_ = std::make_unique<QSettings>(settingsFile, QSettings::IniFormat);
    else if (!demo) settings_ = std::make_unique<QSettings>("OpenLnku", "OpenLnku");
    if (settings_) {
        language_ = settings_->value("language", "zh-CN").toString();
        if (!demo && !settings_->contains("language")) {
            QSettings previous("OpenNex", "OpenNex");
            language_ = previous.value("language", "zh-CN").toString();
        }
    }
    if (!requested.isEmpty()) language_ = requested;
    if (language_ != "en") language_ = "zh-CN";
    QFile file(":/languages/en.json");
    if (!file.open(QIODevice::ReadOnly)) qFatal("English language catalog missing");
    const auto object = QJsonDocument::fromJson(file.readAll()).object();
    if (object.isEmpty()) qFatal("English language catalog invalid");
    for (auto it = object.begin(); it != object.end(); ++it) {
        entries_.insert(it.key(), it.value().toString());
        QRegularExpression argument("%([1-9][0-9]?)");
        auto matches = argument.globalMatch(it.key());
        if (!matches.hasNext()) { prefixes_.append(it.key()); continue; }
        Template item; item.translation = it.value().toString();
        QString pattern = "^"; qsizetype offset = 0;
        while (matches.hasNext()) {
            const auto match = matches.next();
            pattern += QRegularExpression::escape(it.key().mid(offset, match.capturedStart() - offset)) + "([\\s\\S]*?)";
            item.arguments.append(match.captured(1).toInt()); offset = match.capturedEnd();
        }
        pattern += QRegularExpression::escape(it.key().mid(offset)) + "$";
        item.expression = QRegularExpression(pattern); templates_.append(item);
    }
    std::sort(prefixes_.begin(), prefixes_.end(), [](const auto& a, const auto& b) { return a.size() > b.size(); });
}
void Localization::setLanguage(const QString& language) {
    if ((language != "en" && language != "zh-CN") || language == language_) return;
    if (settings_) {
        const bool existed = settings_->contains("language");
        const auto previous = settings_->value("language");
        settings_->setValue("language", language); settings_->sync();
        if (settings_->status() != QSettings::NoError) {
            if (existed) settings_->setValue("language", previous); else settings_->remove("language");
            emit errorRequested(QStringLiteral("无法保存语言设置，已保留原值。")); return;
        }
    }
    language_ = language; emit languageChanged();
}
QString Localization::translate(const QString& source) const { return language_ == "en" ? english(source, 0) : source; }
QString Localization::english(const QString& source, int depth) const {
    if (source.isEmpty() || depth > 12) return source;
    if (const auto it = entries_.constFind(source); it != entries_.cend()) return it.value();
    // Preserve log timestamps, user values and network addresses.
    static const QRegularExpression timestamp("^(\\d{2}:\\d{2}:\\d{2}  )([\\s\\S]*)$");
    if (const auto match = timestamp.match(source); match.hasMatch()) return match.captured(1) + english(match.captured(2), depth + 1);
    for (const auto& item : templates_) {
        const auto match = item.expression.match(source);
        if (!match.hasMatch()) continue;
        QHash<int, QString> arguments;
        for (int i = 0; i < item.arguments.size(); ++i)
            arguments.insert(item.arguments[i], english(match.captured(i + 1), depth + 1));
        static const QRegularExpression token("%([1-9][0-9]?)");
        auto tokens = token.globalMatch(item.translation);
        QString output; qsizetype offset = 0;
        while (tokens.hasNext()) {
            const auto parameter = tokens.next();
            output += item.translation.mid(offset, parameter.capturedStart() - offset);
            output += arguments.value(parameter.captured(1).toInt(), parameter.captured(0));
            offset = parameter.capturedEnd();
        }
        output += item.translation.mid(offset);
        return output;
    }
    for (const auto& prefix : prefixes_) {
        if (source.startsWith(prefix)) return entries_.value(prefix) + english(source.mid(prefix.size()), depth + 1);
    }
    if (source.startsWith(QStringLiteral("："))) return ": " + english(source.mid(1), depth + 1);
    return source;
}
