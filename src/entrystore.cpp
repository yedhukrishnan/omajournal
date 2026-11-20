#include "entrystore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimeZone>

namespace EntryStore {

namespace {

const QString suffix = ".omj";

} // namespace

QString newId() { return QString::fromLatin1(Crypto::randomBytes(16).toHex()); }

QDateTime now() {
    QDateTime time = QDateTime::currentDateTime();
    time.setTime(QTime(time.time().hour(), time.time().minute(), time.time().second()));
    // A fixed offset, so the file says the time as it was where it was written.
    return time.toTimeZone(QTimeZone::fromSecondsAheadOfUtc(time.offsetFromUtc()));
}

QString normalizeTag(const QString &text) {
    static const QRegularExpression valid("^[\\p{L}\\p{N}_][\\p{L}\\p{N}_-]*$");
    QString tag = text.trimmed();
    if (tag.startsWith('#'))
        tag.remove(0, 1);
    tag = tag.toLower();
    return valid.match(tag).hasMatch() && tag.size() <= 64 ? tag : QString();
}

QStringList inlineTags(const QString &body) {
    // A # that starts a word: not a heading's "# ", not part of a URL or of
    // an HTML entity, and not inside code.
    static const QRegularExpression tag("(?<![\\p{L}\\p{N}_#&/])#([\\p{L}\\p{N}_][\\p{L}\\p{N}_-]*)");
    static const QRegularExpression code("`[^`]*`");
    static const QRegularExpression fence("^\\s{0,3}(```|~~~)");
    QStringList tags;
    bool inFence = false;
    for (QString line : body.split('\n')) {
        if (fence.match(line).hasMatch()) {
            inFence = !inFence;
            continue;
        }
        if (inFence)
            continue;
        line.replace(code, " ");
        for (auto it = tag.globalMatch(line); it.hasNext();) {
            const QString found = normalizeTag(it.next().captured(1));
            // A number alone (#1, #42) is a reference, not a tag.
            if (!found.isEmpty() && !tags.contains(found) && !QRegularExpression("^\\d+$").match(found).hasMatch())
                tags << found;
        }
    }
    return tags;
}

QByteArray serialize(const Entry &entry) {
    QByteArray text = "---\ncreated: " + entry.created.toString(Qt::ISODate).toUtf8() + "\n";
    if (!entry.tags.isEmpty())
        text += "tags: [" + entry.tags.join(", ").toUtf8() + "]\n";
    if (entry.location.valid)
        text += "location: { lat: " + QByteArray::number(entry.location.latitude, 'f', 6)
              + ", lon: " + QByteArray::number(entry.location.longitude, 'f', 6)
              + ", accuracy: " + QByteArray::number(entry.location.accuracy, 'f', 0) + " }\n";
    for (const QString &line : entry.otherFrontMatter)
        text += line.toUtf8() + "\n";
    return text + "---\n" + entry.body.toUtf8();
}

std::optional<Entry> parse(const QString &id, const QByteArray &bytes, QString *error) {
    const QString text = QString::fromUtf8(bytes);
    const qsizetype end = text.indexOf("\n---\n");
    if (!text.startsWith("---\n") || end < 0) {
        if (error)
            *error = QObject::tr("The entry has no front matter.");
        return std::nullopt;
    }
    Entry entry;
    entry.id = id;
    entry.body = text.mid(end + 5);
    for (const QString &line : text.mid(4, end - 4).split('\n')) {
        if (line.startsWith("created:")) {
            entry.created = QDateTime::fromString(line.mid(8).trimmed(), Qt::ISODate);
        } else if (line.startsWith("tags:")) {
            QString list = line.mid(5).trimmed();
            if (list.startsWith('[') && list.endsWith(']'))
                list = list.mid(1, list.size() - 2);
            for (const QString &item : list.split(',', Qt::SkipEmptyParts)) {
                const QString tag = normalizeTag(QString(item).remove('"').remove('\''));
                if (!tag.isEmpty() && !entry.tags.contains(tag))
                    entry.tags << tag;
            }
        } else if (line.startsWith("location:")) {
            static const QRegularExpression field("(lat|lon|accuracy)\\s*:\\s*(-?[0-9.]+)");
            bool hasLat = false, hasLon = false;
            for (auto it = field.globalMatch(line); it.hasNext();) {
                const auto m = it.next();
                const double value = m.captured(2).toDouble();
                if (m.captured(1) == "lat") {
                    entry.location.latitude = value;
                    hasLat = true;
                } else if (m.captured(1) == "lon") {
                    entry.location.longitude = value;
                    hasLon = true;
                } else {
                    entry.location.accuracy = value;
                }
            }
            entry.location.valid = hasLat && hasLon;
            // A line this version can't read is kept as it was.
            if (!entry.location.valid)
                entry.otherFrontMatter << line;
        } else {
            entry.otherFrontMatter << line;
        }
    }
    if (!entry.created.isValid()) {
        if (error)
            *error = QObject::tr("The entry has no valid creation time.");
        return std::nullopt;
    }
    return entry;
}

LoadResult loadAll(const QString &directory, const Crypto::SecretKey &key) {
    LoadResult result;
    const QDir dir(directory);
    for (const QFileInfo &info : dir.entryInfoList({"*" + suffix}, QDir::Files, QDir::Name)) {
        // A copy a sync tool made (id.sync-conflict-....omj) still loads, as
        // its own entry, so nothing in it is hidden.
        const QString id = info.fileName().chopped(suffix.size());
        QFile file(info.filePath());
        if (!file.open(QIODevice::ReadOnly)) {
            result.errors << info.fileName() + ": " + file.errorString();
            continue;
        }
        QString error;
        std::optional<QByteArray> plaintext = Crypto::decryptFile(key, file.readAll(), &error);
        std::optional<Entry> entry;
        if (plaintext)
            entry = parse(id, *plaintext, &error);
        if (plaintext)
            Crypto::wipe(*plaintext);
        if (!entry) {
            result.errors << info.fileName() + ": " + error;
            continue;
        }
        result.entries << *entry;
    }
    return result;
}

bool save(const QString &directory, const Crypto::SecretKey &key, const Entry &entry, QString *error) {
    if (entry.id.isEmpty() || entry.id.contains('/') || entry.id.startsWith('.')) {
        if (error)
            *error = QObject::tr("The entry has no id.");
        return false;
    }
    if (!QDir().mkpath(directory)) {
        if (error)
            *error = QObject::tr("Couldn't make the folder %1.").arg(directory);
        return false;
    }
    QByteArray plaintext = serialize(entry);
    const QByteArray encrypted = Crypto::encryptFile(key, plaintext);
    Crypto::wipe(plaintext);

    // Written to a temporary file and renamed over the old one, so a failed
    // save leaves the last good copy. Only the owner may read it, whatever
    // the umask says.
    QSaveFile file(directory + "/" + entry.id + suffix);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)
        || file.write(encrypted) != encrypted.size() || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

} // namespace EntryStore

QStringList Entry::allTags() const {
    QStringList all = tags;
    for (const QString &tag : EntryStore::inlineTags(body))
        if (!all.contains(tag))
            all << tag;
    all.sort();
    return all;
}
