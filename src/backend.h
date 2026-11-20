#pragma once

#include <QDateTime>
#include <QFutureWatcher>
#include <QObject>
#include <QTimer>

#include "entrymodel.h"
#include "entrystore.h"

class QQuickTextDocument;
class Vault;

// The open journal: every entry in the timeline, the one being edited, and
// saving it, encrypted, a moment after typing stops.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(EntryModel *entries READ entries CONSTANT)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QStringList loadErrors READ loadErrors NOTIFY loadingChanged)
    Q_PROPERTY(QString currentId READ currentId NOTIFY currentChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(QDateTime currentCreated READ currentCreated NOTIFY currentChanged)
    // Only changes when another entry opens; typing goes the other way.
    Q_PROPERTY(QString currentText READ currentText NOTIFY currentChanged)
    Q_PROPERTY(SaveState saveState READ saveState NOTIFY saveStateChanged)
    Q_PROPERTY(QString saveError READ saveError NOTIFY saveStateChanged)

public:
    enum SaveState { Saved, Unsaved, Failed };
    Q_ENUM(SaveState)

    explicit Backend(Vault *vault, QObject *parent = nullptr);
    ~Backend() override;

    EntryModel *entries() { return &m_entries; }
    bool loading() const { return m_load.isRunning(); }
    QStringList loadErrors() const { return m_loadErrors; }
    QString currentId() const { return m_current.id; }
    int currentIndex() const { return m_entries.indexOf(m_current.id); }
    QDateTime currentCreated() const { return m_current.created; }
    QString currentText() const { return m_current.body; }
    SaveState saveState() const { return m_saveState; }
    QString saveError() const { return m_saveError; }

    // Starts an entry dated now. It is saved once it has text.
    Q_INVOKABLE void newEntry();
    // False when the open entry couldn't be saved, which keeps it open.
    Q_INVOKABLE bool openEntry(const QString &id);
    Q_INVOKABLE void setText(const QString &text);
    // From the editor's document, so characters the user never touched
    // (non-breaking spaces) come through as they are.
    Q_INVOKABLE void updateFromDocument(QQuickTextDocument *document);
    Q_INVOKABLE bool save();
    // Saves, forgets every decrypted entry, and wipes the key. False, and
    // still open, when the save fails, unless discarding was asked for.
    Q_INVOKABLE bool lock(bool discardUnsaved = false);

    static QString textOf(QQuickTextDocument *document);
    // Makes Backend.Saved and the other states known to QML, as `import Omajournal`.
    static void registerQmlTypes();

signals:
    void loadingChanged();
    void currentChanged();
    void currentIndexChanged();
    void saveStateChanged();

private:
    void load();
    // Saves the open entry and lets it go; false if it couldn't be saved.
    bool leaveCurrent();
    void setCurrent(const Entry &entry, bool isNew);
    void setSaveState(SaveState state, const QString &error = {});

    Vault *m_vault;
    EntryModel m_entries;
    QFutureWatcher<EntryStore::LoadResult> m_load;
    int m_loadGeneration = 0;
    QStringList m_loadErrors;
    Entry m_current;
    QString m_savedBody;
    bool m_currentIsNew = false;
    // Set while one entry gives way to another, since the timeline follows
    // the rows that come and go meanwhile.
    bool m_switching = false;
    SaveState m_saveState = Saved;
    QString m_saveError;
    QTimer m_autosave;
};
