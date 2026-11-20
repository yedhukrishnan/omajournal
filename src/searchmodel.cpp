#include "searchmodel.h"

#include "entrymodel.h"
#include "textfold.h"

SearchModel::SearchModel(QObject *parent) : QSortFilterProxyModel(parent) {
    setDynamicSortFilter(true);
    // Edits refilter, since what an entry says decides whether it matches.
    setFilterRole(EntryModel::BodyRole);
    for (const auto signal : {&QAbstractItemModel::rowsInserted, &QAbstractItemModel::rowsRemoved})
        connect(this, signal, this, &SearchModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &SearchModel::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &SearchModel::countChanged);
    // The cache follows the journal: nothing stays once the entries go.
    connect(this, &QAbstractProxyModel::sourceModelChanged, this, [this] {
        if (sourceModel())
            connect(sourceModel(), &QAbstractItemModel::modelReset, this, [this] { m_folded.clear(); });
    });
}

void SearchModel::setQuery(const QString &query) {
    if (query == m_query)
        return;
    m_query = query;
    const QStringList terms = TextFold::terms(query);
    if (terms != m_terms) {
        beginFilterChange();
        m_terms = terms;
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
        // Snippets depend on the words as well as on which rows remain.
        if (rowCount() > 0)
            emit dataChanged(index(0, 0), index(rowCount() - 1, 0), {EntryModel::SnippetRole});
    }
    emit queryChanged();
    emit countChanged();
}

QString SearchModel::folded(const QString &id, const QString &body) const {
    auto it = m_folded.find(id);
    if (it == m_folded.end() || it->first != body)
        it = m_folded.insert(id, {body, TextFold::fold(body)});
    return it->second;
}

bool SearchModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const {
    if (m_terms.isEmpty())
        return true;
    const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
    const QString text = folded(index.data(EntryModel::IdRole).toString(), index.data(EntryModel::BodyRole).toString());
    for (const QString &term : m_terms)
        if (!text.contains(term))
            return false;
    return true;
}

QVariant SearchModel::data(const QModelIndex &index, int role) const {
    if (role == EntryModel::SnippetRole && active())
        return snippet(QSortFilterProxyModel::data(index, EntryModel::BodyRole).toString(), m_terms);
    return QSortFilterProxyModel::data(index, role);
}

int SearchModel::indexOf(const QString &id) const {
    for (int row = 0; row < rowCount(); ++row)
        if (index(row, 0).data(EntryModel::IdRole).toString() == id)
            return row;
    return -1;
}

QString SearchModel::idAt(int row) const {
    return row >= 0 && row < rowCount() ? index(row, 0).data(EntryModel::IdRole).toString() : QString();
}

QString SearchModel::snippet(const QString &body, const QStringList &terms) {
    // The title shows above the snippet already, so the snippet comes from
    // what follows it, unless only the title matched.
    const QString whole = EntryModel::plainText(body);
    const QString title = EntryModel::title(body);
    QString text = whole.startsWith(title) ? whole.mid(title.size()).trimmed() : whole;
    QList<TextFold::Match> matches = TextFold::find(text, terms);
    if (matches.isEmpty()) {
        text = whole;
        matches = TextFold::find(text, terms);
    }
    if (matches.isEmpty())
        return EntryModel::excerpt(body).toHtmlEscaped();

    // Some words of context before the first match, then what follows.
    int start = std::max(0, matches.first().start - 40);
    if (start > 0) {
        const qsizetype space = text.indexOf(' ', start);
        if (space >= 0 && space < matches.first().start)
            start = int(space + 1);
    }
    const int end = std::min<int>(int(text.size()), start + 200);

    QString html = start > 0 ? QStringLiteral("…") : QString();
    int at = start;
    for (const TextFold::Match &match : matches) {
        if (match.start < at)
            continue;
        if (match.start >= end)
            break;
        html += text.mid(at, match.start - at).toHtmlEscaped();
        html += "<b>" + text.mid(match.start, match.length).toHtmlEscaped() + "</b>";
        at = match.start + match.length;
    }
    html += text.mid(at, end - at).toHtmlEscaped();
    if (end < text.size())
        html += QStringLiteral("…");
    return html;
}
