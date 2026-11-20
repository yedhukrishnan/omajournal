#include "markdownhighlighter.h"

#include <QRegularExpression>
#include <QTextDocument>

#include "textfold.h"

namespace {

// The marks a line can start with, and the spans inside a line.
const QRegularExpression &fence() {
    static const QRegularExpression re("^\\s{0,3}(```|~~~)");
    return re;
}
const QRegularExpression &heading() {
    static const QRegularExpression re("^(#{1,6})(\\s+)(.*)$");
    return re;
}
const QRegularExpression &rule() {
    static const QRegularExpression re("^\\s{0,3}([-*_])(\\s*\\1){2,}\\s*$");
    return re;
}
const QRegularExpression &quote() {
    static const QRegularExpression re("^(\\s*>+\\s?)");
    return re;
}
const QRegularExpression &listItem() {
    static const QRegularExpression re("^(\\s*)([-*+]|\\d{1,9}[.)])(\\s+)(\\[[ xX]\\](?=\\s|$))?");
    return re;
}
const QRegularExpression &codeSpan() {
    static const QRegularExpression re("`([^`]+)`");
    return re;
}
const QRegularExpression &bold() {
    static const QRegularExpression re("(\\*\\*|__)(?=\\S)(.+?)(?<=\\S)\\1");
    return re;
}
const QRegularExpression &italicStar() {
    static const QRegularExpression re("(?<![*\\\\])\\*(?=[^\\s*])(.+?)(?<=[^\\s*])\\*(?!\\*)");
    return re;
}
const QRegularExpression &italicUnderscore() {
    static const QRegularExpression re("(?<![\\w_])_(?=\\S)(.+?)(?<=\\S)_(?![\\w_])");
    return re;
}
const QRegularExpression &strike() {
    static const QRegularExpression re("~~(?=\\S)(.+?)(?<=\\S)~~");
    return re;
}
const QRegularExpression &link() {
    static const QRegularExpression re("\\[([^\\]]+)\\]\\(([^)\\s]+)\\)");
    return re;
}
const QRegularExpression &bareLink() {
    static const QRegularExpression re("\\bhttps?://[^\\s<>()]+[^\\s<>().,;:!?'\"]");
    return re;
}

// A line's formats, one per character, merged as rules apply, then handed
// to Qt as runs.
class Styler {
public:
    explicit Styler(int length) : m_formats(length), m_protected(length, false) {}

    void apply(int start, int length, const QTextCharFormat &format) {
        for (int i = std::max(0, start); i < std::min<int>(start + length, m_formats.size()); ++i)
            m_formats[i].merge(format);
    }
    // Code spans keep their text literal: no emphasis inside them.
    void protect(int start, int length) {
        for (int i = start; i < start + length; ++i)
            m_protected[i] = true;
    }
    bool isProtected(int start, int length) const {
        for (int i = start; i < start + length; ++i)
            if (m_protected.at(i))
                return true;
        return false;
    }
    template <typename SetFormat>
    void commit(SetFormat setFormat) const {
        int start = 0;
        for (int i = 1; i <= m_formats.size(); ++i) {
            if (i == m_formats.size() || m_formats.at(i) != m_formats.at(start)) {
                if (m_formats.at(start) != QTextCharFormat())
                    setFormat(start, i - start, m_formats.at(start));
                start = i;
            }
        }
    }

private:
    QList<QTextCharFormat> m_formats;
    QList<bool> m_protected;
};

} // namespace

MarkdownHighlighter::MarkdownHighlighter(QObject *parent) : QSyntaxHighlighter(parent) {}

void MarkdownHighlighter::setTextDocument(QQuickTextDocument *document) {
    if (document == m_textDocument)
        return;
    m_textDocument = document;
    setDocument(document ? document->textDocument() : nullptr);
    emit changed();
}

#define SETTER(name, member, type)                                                                                     \
    void MarkdownHighlighter::name(const type &value) {                                                                \
        if (member == value)                                                                                           \
            return;                                                                                                    \
        member = value;                                                                                                \
        restyle();                                                                                                     \
    }
SETTER(setForeground, m_foreground, QColor)
SETTER(setMuted, m_muted, QColor)
SETTER(setAccent, m_accent, QColor)
SETTER(setCodeBackground, m_codeBackground, QColor)
SETTER(setMatchBackground, m_matchBackground, QColor)
#undef SETTER

void MarkdownHighlighter::setBaseSize(int size) {
    if (size == m_baseSize || size <= 0)
        return;
    m_baseSize = size;
    restyle();
}

void MarkdownHighlighter::setSearch(const QString &search) {
    if (search == m_search)
        return;
    m_search = search;
    const QStringList terms = TextFold::terms(search);
    if (terms == m_terms) {
        emit changed();
        return;
    }
    m_terms = terms;
    restyle();
}

void MarkdownHighlighter::restyle() {
    if (document())
        rehighlight();
    emit changed();
}

void MarkdownHighlighter::highlightBlock(const QString &text) {
    const bool wasInFence = previousBlockState() == InFence;
    const bool isFence = fence().match(text).hasMatch();
    setCurrentBlockState(wasInFence != isFence ? InFence : Normal);
    if (text.size() > longLine)
        return;

    QTextCharFormat muted;
    muted.setForeground(m_muted);
    QTextCharFormat mono;
    mono.setFontFamilies({"monospace"});
    mono.setFontFixedPitch(true);
    QTextCharFormat accent;
    accent.setForeground(m_accent);

    Styler styler(int(text.size()));

    if (wasInFence || isFence) {
        // A fence's own line is a mark; the lines between are code.
        styler.apply(0, int(text.size()), mono);
        if (isFence)
            styler.apply(0, int(text.size()), muted);
    } else if (rule().match(text).hasMatch()) {
        styler.apply(0, int(text.size()), muted);
    } else {
        if (const auto m = heading().match(text); m.hasMatch()) {
            const int level = int(m.capturedLength(1));
            static const double scale[] = {1.6, 1.35, 1.2, 1.08, 1.0, 1.0};
            QTextCharFormat format;
            format.setFontWeight(QFont::Bold);
            format.setProperty(QTextFormat::FontPixelSize, int(m_baseSize * scale[level - 1] + 0.5));
            styler.apply(0, int(text.size()), format);
            styler.apply(0, int(m.capturedEnd(2)), muted);
        } else if (const auto q = quote().match(text); q.hasMatch()) {
            QTextCharFormat italic;
            italic.setFontItalic(true);
            styler.apply(0, int(text.size()), italic);
            styler.apply(0, int(q.capturedLength(1)), accent);
        } else if (const auto l = listItem().match(text); l.hasMatch()) {
            QTextCharFormat marker = accent;
            marker.setFontWeight(QFont::Bold);
            styler.apply(int(l.capturedStart(2)), int(l.capturedLength(2)), marker);
            if (l.capturedLength(4) > 0) {
                styler.apply(int(l.capturedStart(4)), int(l.capturedLength(4)), mono);
                // A checked task reads as done.
                if (l.captured(4).contains(QRegularExpression("[xX]"))) {
                    QTextCharFormat done = muted;
                    done.setFontStrikeOut(true);
                    styler.apply(int(l.capturedEnd(4)), int(text.size() - l.capturedEnd(4)), done);
                }
            }
        }

        for (auto it = codeSpan().globalMatch(text); it.hasNext();) {
            const auto m = it.next();
            QTextCharFormat code = mono;
            code.setBackground(m_codeBackground);
            styler.apply(int(m.capturedStart()), int(m.capturedLength()), code);
            styler.apply(int(m.capturedStart()), 1, muted);
            styler.apply(int(m.capturedEnd()) - 1, 1, muted);
            styler.protect(int(m.capturedStart()), int(m.capturedLength()));
        }

        // Emphasis: the marks dim, the text between takes the style.
        const auto emphasis = [&](const QRegularExpression &re, const QTextCharFormat &format, int markGroup) {
            for (auto it = re.globalMatch(text); it.hasNext();) {
                const auto m = it.next();
                if (styler.isProtected(int(m.capturedStart()), int(m.capturedLength())))
                    continue;
                const int mark = markGroup ? int(m.capturedLength(markGroup))
                                           : int(m.capturedStart(1) - m.capturedStart());
                styler.apply(int(m.capturedStart()), int(m.capturedLength()), format);
                styler.apply(int(m.capturedStart()), mark, muted);
                styler.apply(int(m.capturedEnd()) - mark, mark, muted);
            }
        };
        QTextCharFormat boldFormat;
        boldFormat.setFontWeight(QFont::Bold);
        QTextCharFormat italicFormat;
        italicFormat.setFontItalic(true);
        QTextCharFormat strikeFormat;
        strikeFormat.setFontStrikeOut(true);
        emphasis(bold(), boldFormat, 1);
        emphasis(italicStar(), italicFormat, 0);
        emphasis(italicUnderscore(), italicFormat, 0);
        emphasis(strike(), strikeFormat, 0);

        for (auto it = link().globalMatch(text); it.hasNext();) {
            const auto m = it.next();
            if (styler.isProtected(int(m.capturedStart()), int(m.capturedLength())))
                continue;
            QTextCharFormat label = accent;
            label.setFontUnderline(true);
            styler.apply(int(m.capturedStart()), int(m.capturedLength()), muted);
            styler.apply(int(m.capturedStart(1)), int(m.capturedLength(1)), label);
            // Its address isn't styled again as a bare link.
            styler.protect(int(m.capturedStart()), int(m.capturedLength()));
        }
        for (auto it = bareLink().globalMatch(text); it.hasNext();) {
            const auto m = it.next();
            if (styler.isProtected(int(m.capturedStart()), int(m.capturedLength())))
                continue;
            QTextCharFormat url = accent;
            url.setFontUnderline(true);
            styler.apply(int(m.capturedStart()), int(m.capturedLength()), url);
        }
    }

    for (const TextFold::Match &match : TextFold::find(text, m_terms)) {
        QTextCharFormat found;
        found.setBackground(m_matchBackground);
        styler.apply(match.start, match.length, found);
    }

    styler.commit([this](int start, int length, const QTextCharFormat &format) { setFormat(start, length, format); });
}
