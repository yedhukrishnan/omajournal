#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// Text folded for matching: case folded, compatibility-decomposed, and
// stripped of accents, so "Cafe" finds "café" and "ﬁ" finds "fi".
namespace TextFold {

// The folded text; map, when given, gets for each folded character the index
// of the character in text it came from.
QString fold(const QString &text, QList<int> *map = nullptr);
// The folded words of a search, longest first, without duplicates.
QStringList terms(const QString &query);

struct Match {
    int start;  // in the original text
    int length; // in the original text
};
// Every place in text where one of the folded terms occurs, in order and
// without overlaps.
QList<Match> find(const QString &text, const QStringList &terms);

} // namespace TextFold
