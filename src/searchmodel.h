#pragma once

#include <QHash>
#include <QSortFilterProxyModel>
#include <QStringList>

// The timeline narrowed to the entries that hold every word of a search,
// whatever their case or accents, newest first. Each result's snippet shows
// where it matched, with the matches in bold.
class SearchModel : public QSortFilterProxyModel {
    Q_OBJECT
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(bool active READ active NOTIFY queryChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit SearchModel(QObject *parent = nullptr);

    QString query() const { return m_query; }
    void setQuery(const QString &query);
    bool active() const { return !m_terms.isEmpty(); }
    int count() const { return rowCount(); }

    QVariant data(const QModelIndex &index, int role) const override;
    Q_INVOKABLE int indexOf(const QString &id) const;
    Q_INVOKABLE QString idAt(int row) const;

    // Around the first match in the plain text of a body, as styled text.
    static QString snippet(const QString &body, const QStringList &terms);

signals:
    void queryChanged();
    void countChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    QString folded(const QString &id, const QString &body) const;

    QString m_query;
    QStringList m_terms;
    // Folded bodies by entry, kept while the body is unchanged.
    mutable QHash<QString, QPair<QString, QString>> m_folded;
};
