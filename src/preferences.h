#pragma once

#include <QObject>
#include <QStringList>

// How the editor shows entries: the writing font and its size, kept in
// settings.json. Only the look changes; entries stay plain Markdown, and the
// rest of the window keeps the desktop's font.
class Preferences : public QObject {
    Q_OBJECT
    // A family, a generic name ("serif", "monospace"), or empty for the
    // desktop's interface font.
    Q_PROPERTY(QString fontFamily READ fontFamily WRITE setFontFamily NOTIFY fontChanged)
    // Pixels, for the body text; headings scale from it.
    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY fontChanged)
    // False when the chosen family isn't installed (any more), so Qt draws
    // another in its place.
    Q_PROPERTY(bool fontInstalled READ fontInstalled NOTIFY fontChanged)
    // The installed families, sorted, without the ones apps aren't meant to use.
    Q_PROPERTY(QStringList fontFamilies READ fontFamilies CONSTANT)
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
    bool fontInstalled() const;
    QStringList fontFamilies() const;
    QString saveError() const { return m_saveError; }

    Q_INVOKABLE void enlargeFont() { setFontSize(m_fontSize + 1); }
    Q_INVOKABLE void shrinkFont() { setFontSize(m_fontSize - 1); }
    Q_INVOKABLE void resetFontSize() { setFontSize(defaultSize); }

signals:
    void fontChanged();
    void saveErrorChanged();

private:
    void save();

    QString m_settingsFile;
    QString m_fontFamily;
    int m_fontSize = defaultSize;
    QString m_saveError;
    mutable QStringList m_families;
};
