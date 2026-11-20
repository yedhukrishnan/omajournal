#pragma once

#include <QColor>
#include <QPointer>
#include <QQuickTextDocument>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

// Styles Markdown in the editor as it is typed: headings larger and bold,
// bold, italic, struck-through, code in a monospace face, lists, quotes and
// links, with the marks themselves dimmed. Only how the text looks changes;
// the document keeps exactly what was typed. Search terms are highlighted too.
class MarkdownHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
    Q_PROPERTY(QQuickTextDocument *textDocument READ textDocument WRITE setTextDocument NOTIFY changed)
    Q_PROPERTY(QColor foreground MEMBER m_foreground WRITE setForeground NOTIFY changed)
    Q_PROPERTY(QColor muted MEMBER m_muted WRITE setMuted NOTIFY changed)
    Q_PROPERTY(QColor accent MEMBER m_accent WRITE setAccent NOTIFY changed)
    Q_PROPERTY(QColor codeBackground MEMBER m_codeBackground WRITE setCodeBackground NOTIFY changed)
    Q_PROPERTY(QColor matchBackground MEMBER m_matchBackground WRITE setMatchBackground NOTIFY changed)
    Q_PROPERTY(int baseSize MEMBER m_baseSize WRITE setBaseSize NOTIFY changed)
    // A search, as typed; its words are highlighted wherever they occur.
    Q_PROPERTY(QString search MEMBER m_search WRITE setSearch NOTIFY changed)

public:
    explicit MarkdownHighlighter(QObject *parent = nullptr);

    QQuickTextDocument *textDocument() const { return m_textDocument; }
    void setTextDocument(QQuickTextDocument *document);
    void setForeground(const QColor &color);
    void setMuted(const QColor &color);
    void setAccent(const QColor &color);
    void setCodeBackground(const QColor &color);
    void setMatchBackground(const QColor &color);
    void setBaseSize(int size);
    void setSearch(const QString &search);

    // Lines longer than this stay unstyled: Qt lays out a line's formats in
    // quadratic time. They are still scanned for code fences.
    static constexpr int longLine = 3000;

    // Block states: whether a line is inside a ``` fence.
    enum State { Normal = 0, InFence = 1 };

signals:
    void changed();

protected:
    void highlightBlock(const QString &text) override;

private:
    void restyle();

    QPointer<QQuickTextDocument> m_textDocument;
    QColor m_foreground{"#e6e6e9"};
    QColor m_muted{"#5c5c66"};
    QColor m_accent{"#FFD60A"};
    QColor m_codeBackground{"#3a3a40"};
    QColor m_matchBackground{"#806b05"};
    int m_baseSize = 16;
    QString m_search;
    QStringList m_terms;
};
