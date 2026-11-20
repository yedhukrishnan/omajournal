#include "entrymodel.h"

#include <QRegularExpression>
#include <algorithm>

namespace {

// Markdown marks that would only be noise in a one-line preview.
QString plain(QString line) {
    static const QRegularExpression leading("^\\s*(#{1,6}\\s+|>\\s*|[-*+]\\s+(\\[[ xX]\\]\\s+)?|\\d+[.)]\\s+)");
    static const QRegularExpression emphasis("(\\*\\*|__|\\*|_|`|~~)");
    line.remove(leading);
    line.remove(emphasis);
    return line.trimmed();
}

} // namespace

int EntryModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : count(); }

QVariant EntryModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= count())
        return {};
    const Entry &entry = m_entries.at(index.row());
    switch (role) {
    case IdRole:
        return entry.id;
    case TitleRole:
        return title(entry.body);
    case ExcerptRole:
        return excerpt(entry.body);
    case CreatedRole:
        return entry.created;
    case DayRole:
        // The day where it was written, which the stored offset keeps.
        return entry.created.date().toString(Qt::ISODate);
    }
    return {};
}

QHash<int, QByteArray> EntryModel::roleNames() const {
    return {{IdRole, "entryId"}, {TitleRole, "title"}, {ExcerptRole, "excerpt"}, {CreatedRole, "created"},
            {DayRole, "day"}};
}

bool EntryModel::newerFirst(const Entry &a, const Entry &b) {
    if (a.created != b.created)
        return a.created > b.created;
    return a.id > b.id;
}

void EntryModel::setEntries(QList<Entry> entries) {
    std::sort(entries.begin(), entries.end(), newerFirst);
    beginResetModel();
    m_entries = std::move(entries);
    endResetModel();
    emit countChanged();
}

void EntryModel::clear() { setEntries({}); }

void EntryModel::upsert(const Entry &entry) {
    if (const int row = indexOf(entry.id); row >= 0) {
        m_entries[row] = entry;
        emit dataChanged(index(row), index(row), {TitleRole, ExcerptRole});
        return;
    }
    const auto at = std::lower_bound(m_entries.begin(), m_entries.end(), entry, newerFirst);
    const int row = int(at - m_entries.begin());
    beginInsertRows({}, row, row);
    m_entries.insert(row, entry);
    endInsertRows();
    emit countChanged();
}

void EntryModel::remove(const QString &id) {
    const int row = indexOf(id);
    if (row < 0)
        return;
    beginRemoveRows({}, row, row);
    m_entries.removeAt(row);
    endRemoveRows();
    emit countChanged();
}

int EntryModel::indexOf(const QString &id) const {
    for (int row = 0; row < count(); ++row)
        if (m_entries.at(row).id == id)
            return row;
    return -1;
}

const Entry *EntryModel::find(const QString &id) const {
    const int row = indexOf(id);
    return row < 0 ? nullptr : &m_entries.at(row);
}

int EntryModel::firstRowOnDay(const QString &day) const {
    for (int row = 0; row < count(); ++row)
        if (m_entries.at(row).created.date().toString(Qt::ISODate) == day)
            return row;
    return -1;
}

QVariantMap EntryModel::entryDays() const {
    QVariantMap days;
    for (const Entry &entry : m_entries) {
        const QString day = entry.created.date().toString(Qt::ISODate);
        days.insert(day, days.value(day).toInt() + 1);
    }
    return days;
}

QString EntryModel::idAt(int row) const { return row >= 0 && row < count() ? m_entries.at(row).id : QString(); }

QString EntryModel::title(const QString &body) {
    for (const QString &line : body.split('\n')) {
        const QString text = plain(line);
        if (!text.isEmpty())
            return text.left(120);
    }
    return {};
}

QString EntryModel::excerpt(const QString &body) {
    QStringList words;
    bool pastTitle = false;
    int length = 0;
    for (const QString &line : body.split('\n')) {
        const QString text = plain(line);
        if (text.isEmpty())
            continue;
        if (!pastTitle) {
            pastTitle = true;
            continue;
        }
        words << text;
        length += text.size() + 1;
        if (length > 200)
            break;
    }
    return words.join(' ').left(200);
}
