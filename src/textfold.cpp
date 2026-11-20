#include "textfold.h"

#include <algorithm>

namespace TextFold {

QString fold(const QString &text, QList<int> *map) {
    QString folded;
    folded.reserve(text.size());
    if (map) {
        map->clear();
        map->reserve(text.size());
    }
    for (int i = 0; i < text.size(); ++i) {
        // A surrogate pair is one character.
        const int length = text.at(i).isHighSurrogate() && i + 1 < text.size() ? 2 : 1;
        const QString decomposed = text.mid(i, length).normalized(QString::NormalizationForm_KD);
        for (const QChar c : decomposed) {
            if (c.category() == QChar::Mark_NonSpacing)
                continue;
            for (const QChar f : QString(c).toCaseFolded()) {
                folded += f;
                if (map)
                    map->append(i);
            }
        }
        i += length - 1;
    }
    return folded;
}

QStringList terms(const QString &query) {
    QStringList words;
    for (const QString &word : fold(query).split(' ', Qt::SkipEmptyParts)) {
        const QString trimmed = word.trimmed();
        if (!trimmed.isEmpty() && !words.contains(trimmed))
            words << trimmed;
    }
    // Longest first, so "pages" wins over "page" where both match.
    std::stable_sort(words.begin(), words.end(), [](const QString &a, const QString &b) { return a.size() > b.size(); });
    return words;
}

QList<Match> find(const QString &text, const QStringList &terms) {
    QList<Match> matches;
    if (terms.isEmpty() || text.isEmpty())
        return matches;
    QList<int> map;
    const QString folded = fold(text, &map);
    QList<bool> taken(folded.size(), false);
    struct Span {
        int start, end; // in folded
    };
    QList<Span> spans;
    for (const QString &term : terms) {
        for (qsizetype at = folded.indexOf(term); at >= 0; at = folded.indexOf(term, at + 1)) {
            const int end = int(at + term.size());
            if (std::any_of(taken.begin() + at, taken.begin() + end, [](bool t) { return t; }))
                continue;
            std::fill(taken.begin() + at, taken.begin() + end, true);
            spans.append({int(at), end});
        }
    }
    std::sort(spans.begin(), spans.end(), [](const Span &a, const Span &b) { return a.start < b.start; });
    for (const Span &span : spans) {
        const int start = map.at(span.start);
        const int last = map.at(span.end - 1);
        // The original character the match ends in, whole (a surrogate pair is two).
        const int end = last + (text.at(last).isHighSurrogate() && last + 1 < text.size() ? 2 : 1);
        matches.append({start, end - start});
    }
    return matches;
}

} // namespace TextFold
