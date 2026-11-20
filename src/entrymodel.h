#pragma once

#include <QAbstractListModel>

#include "entrystore.h"

// Every entry, newest first, for the timeline. The list groups rows into
// sections by their day.
class EntryModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role { IdRole = Qt::UserRole + 1, TitleRole, ExcerptRole, CreatedRole, DayRole, BodyRole, SnippetRole };

    explicit EntryModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return int(m_entries.size()); }

    void setEntries(QList<Entry> entries);
    void clear();
    // Adds the entry in its place, or updates the one with its id.
    void upsert(const Entry &entry);
    void remove(const QString &id);
    Q_INVOKABLE int indexOf(const QString &id) const;
    const Entry *find(const QString &id) const;
    Q_INVOKABLE QString idAt(int row) const;
    // The row of the newest entry written on a day (yyyy-MM-dd), or -1.
    int firstRowOnDay(const QString &day) const;
    // How many entries each day (yyyy-MM-dd) has.
    QVariantMap entryDays() const;

    // The first line that has text, without Markdown's heading marks.
    static QString title(const QString &body);
    // A little of what follows the title, as one line of plain text.
    static QString excerpt(const QString &body);
    // The whole body as one line of plain text, without Markdown's marks.
    static QString plainText(const QString &body);

signals:
    void countChanged();

private:
    static bool newerFirst(const Entry &a, const Entry &b);
    QList<Entry> m_entries;
};
