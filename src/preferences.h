#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>

// How the editor shows entries: the writing font and its size, kept in
// settings.json. Only the look changes; entries stay plain Markdown, and the
// rest of the window keeps the desktop's font.
class Preferences : public QObject {
    Q_OBJECT
    // A family, a generic name ("serif", "monospace"), or empty for the
    // desktop's interface font.
    Q_PROPERTY(QString fontFamily READ fontFamily WRITE setFontFamily NOTIFY fontChanged)
    // What the editor shows: a font being tried in the picker, which is never
    // saved, or else fontFamily.
    Q_PROPERTY(QString editorFamily READ editorFamily NOTIFY fontChanged)
    // The style the editor asks for by name; see regularStyle().
    Q_PROPERTY(QString editorStyle READ editorStyle NOTIFY fontChanged)
    // Pixels, for the body text; headings scale from it.
    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY fontChanged)
    // False when the chosen family isn't installed (any more), so Qt draws
    // another in its place.
    Q_PROPERTY(bool fontInstalled READ fontInstalled NOTIFY fontChanged)
    // The short list the picker offers, as [{family, name, detail}]: the
    // desktop's font, iA Writer's when installed, serif and monospace, and
    // the chosen font if it's none of those.
    Q_PROPERTY(QVariantList fontChoices READ fontChoices NOTIFY fontChanged)
    Q_PROPERTY(QString saveError READ saveError NOTIFY saveErrorChanged)

public:
    static constexpr int defaultSize = 16;
    static constexpr int minimumSize = 10;
    static constexpr int maximumSize = 40;

    // Tests pass their own settings file.
    explicit Preferences(const QString &settingsFile = {}, QObject *parent = nullptr);

    QString fontFamily() const { return m_fontFamily; }
    void setFontFamily(const QString &family);
    int fontSize() const { return m_fontSize; }
    void setFontSize(int size);
    QString editorFamily() const { return m_previewing ? m_previewFamily : m_fontFamily; }
    QString editorStyle() const { return regularStyle(editorFamily()); }
    bool fontInstalled() const;
    QStringList fontFamilies() const;
    QVariantList fontChoices() const;
    QString saveError() const { return m_saveError; }

    Q_INVOKABLE void enlargeFont() { setFontSize(m_fontSize + 1); }
    Q_INVOKABLE void shrinkFont() { setFontSize(m_fontSize - 1); }
    Q_INVOKABLE void resetFontSize() { setFontSize(defaultSize); }
    // Shows a font in the editor without choosing it, until endPreview().
    Q_INVOKABLE void previewFont(const QString &family);
    Q_INVOKABLE void endPreview();
    // The installed family a name stands for: the desktop's interface font
    // for an empty one, fontconfig's pick for "serif" and "monospace".
    Q_INVOKABLE static QString resolvedFamily(const QString &family);
    // "Regular" for a family whose files are labelled so that asking for the
    // normal weight finds its bold or italic face (iA Writer Quattro S, as
    // packaged, marks its Bold as normal weight); empty for every other one.
    Q_INVOKABLE static QString regularStyle(const QString &family);

signals:
    void fontChanged();
    void saveErrorChanged();

private:
    void save();

    QString m_settingsFile;
    QString m_fontFamily;
    int m_fontSize = defaultSize;
    QString m_previewFamily;
    bool m_previewing = false;
    QString m_saveError;
    mutable QStringList m_families;
};
