#pragma once
#include <QObject>
#include <QHash>
#include <QSettings>
#include <QRegularExpression>
#include <memory>

// State and logs stay in their canonical language; presentation is translated on
// demand, so changing language does not alter network state or discard history.
class Localization : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
public:
    explicit Localization(bool demo, const QString& settingsFile = {}, const QString& requested = {}, QObject* parent = nullptr);
    QString language() const { return language_; }
    void setLanguage(const QString& language);
    Q_INVOKABLE QString translate(const QString& source) const;
signals:
    void languageChanged();
    void errorRequested(const QString& message);
private:
    struct Template { QRegularExpression expression; QString translation; QList<int> arguments; };
    QString english(const QString& source, int depth) const;
    QString language_ = "zh-CN";
    QHash<QString, QString> entries_;
    QStringList prefixes_;
    QList<Template> templates_;
    std::unique_ptr<QSettings> settings_;
};
