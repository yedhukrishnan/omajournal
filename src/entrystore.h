#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

#include "crypto.h"

// One journal entry as it is inside its encrypted file: front matter, then
// the Markdown body.
struct Entry {
    QString id;
    QDateTime created;
    QString body;
    // Front matter lines this version doesn't use yet (tags, location written
    // by a later version), kept so saving here never drops them.
    QStringList otherFrontMatter;

    bool operator==(const Entry &other) const {
        return id == other.id && created == other.created && body == other.body
            && otherFrontMatter == other.otherFrontMatter;
    }
};

// Reads and writes entries as e/<id>.omj in the journal folder. Every
// function is safe to call from a worker thread.
namespace EntryStore {

// A random id, which is also the file name, so names say nothing about dates.
QString newId();
// The time an entry is created: now, to the second, with its UTC offset.
QDateTime now();

QByteArray serialize(const Entry &entry);
std::optional<Entry> parse(const QString &id, const QByteArray &text, QString *error = nullptr);

struct LoadResult {
    QList<Entry> entries;
    // One line per file that couldn't be read, naming it.
    QStringList errors;
};
LoadResult loadAll(const QString &directory, const Crypto::SecretKey &key);
bool save(const QString &directory, const Crypto::SecretKey &key, const Entry &entry, QString *error = nullptr);

} // namespace EntryStore
