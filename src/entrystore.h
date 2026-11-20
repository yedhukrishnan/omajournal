#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

#include "crypto.h"

// Where an entry was written, as the desktop's location service gave it.
struct Location {
    double latitude = 0;
    double longitude = 0;
    double accuracy = 0; // metres; 0 when unknown
    bool valid = false;

    bool operator==(const Location &other) const {
        return valid == other.valid
            && (!valid || (latitude == other.latitude && longitude == other.longitude && accuracy == other.accuracy));
    }
};

// One journal entry as it is inside its encrypted file: front matter, then
// the Markdown body.
struct Entry {
    Entry() = default;
    Entry(QString id, QDateTime created, QString body, QStringList otherFrontMatter = {})
        : id(std::move(id)), created(std::move(created)), body(std::move(body)),
          otherFrontMatter(std::move(otherFrontMatter)) {}

    QString id;
    QDateTime created;
    QString body;
    // Front matter lines this version doesn't use yet (tags, location written
    // by a later version), kept so saving here never drops them.
    QStringList otherFrontMatter;
    // Tags set on the entry itself; #tags in the body count too (allTags).
    QStringList tags;
    Location location;

    bool operator==(const Entry &other) const {
        return id == other.id && created == other.created && created.offsetFromUtc() == other.created.offsetFromUtc()
            && body == other.body && otherFrontMatter == other.otherFrontMatter && tags == other.tags
            && location == other.location;
    }
    // Nothing written and nothing set: a fresh page.
    bool isBlank() const { return body.isEmpty() && tags.isEmpty() && !location.valid; }
    // The entry's own tags and the #tags in its body, without duplicates, sorted.
    QStringList allTags() const;
};

// Reads and writes entries as e/<id>.omj in the journal folder. Every
// function is safe to call from a worker thread.
namespace EntryStore {

// A random id, which is also the file name, so names say nothing about dates.
QString newId();
// The time an entry is created: now, to the second, with its UTC offset.
QDateTime now();

// A tag as stored: lower case, without its #, of letters, digits, _ and -.
// Empty when the text can't be a tag.
QString normalizeTag(const QString &text);
// The #tags written in a body, outside code.
QStringList inlineTags(const QString &body);

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
