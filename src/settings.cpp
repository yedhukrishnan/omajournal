#include "settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

namespace Settings {

QString defaultFile() { return QDir::homePath() + "/.config/omajournal/settings.json"; }

QJsonObject read(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool update(const QString &path, const QJsonObject &changes, QString *error) {
    QJsonObject settings = read(path);
    for (auto it = changes.begin(); it != changes.end(); ++it) {
        if (it.value().isNull())
            settings.remove(it.key());
        else
            settings[it.key()] = it.value();
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    const QByteArray contents = QJsonDocument(settings).toJson();
    if (file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size() && file.commit())
        return true;
    if (error)
        *error = file.errorString();
    return false;
}

} // namespace Settings
