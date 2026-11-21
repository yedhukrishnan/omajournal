#pragma once

#include <QJsonObject>
#include <QString>

// settings.json in ~/.config/omajournal: the journal's folder, which the
// vault keeps, and the preferences. Each writer changes only its own keys.
namespace Settings {

QString defaultFile();
// Empty when the file is missing or isn't JSON.
QJsonObject read(const QString &path);
// Sets the keys in changes, keeping the rest; a null value removes its key.
bool update(const QString &path, const QJsonObject &changes, QString *error = nullptr);

} // namespace Settings
