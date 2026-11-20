#pragma once

#include <QDateTime>
#include <QFutureWatcher>
#include <QObject>
#include <QTimer>

#include "entrymodel.h"
#include "entrystore.h"
#include "searchmodel.h"

class LocationSource;
class QQuickTextDocument;
class Vault;

// The open journal: every entry in the timeline, the one being edited, and
// saving it, encrypted, a moment after typing stops.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(EntryModel *entries READ entries CONSTANT)
    // The timeline narrowed by a search.
    Q_PROPERTY(SearchModel *search READ search CONSTANT)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QStringList loadErrors READ loadErrors NOTIFY loadingChanged)
    Q_PROPERTY(QString currentId READ currentId NOTIFY currentChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(QDateTime currentCreated READ currentCreated NOTIFY metadataChanged)
    // The day the open entry was written (yyyy-MM-dd), where it was written.
    Q_PROPERTY(QString currentDay READ currentDay NOTIFY metadataChanged)
    // Its date and time as written, for editing: yyyy-MM-dd and HH:mm.
    Q_PROPERTY(QString currentTime READ currentTime NOTIFY metadataChanged)
    // Tags set on the open entry, and the #tags in its text.
    Q_PROPERTY(QStringList currentTags READ currentTags NOTIFY metadataChanged)
    Q_PROPERTY(QStringList currentInlineTags READ currentInlineTags NOTIFY metadataChanged)
    // {latitude, longitude, accuracy}, or empty without one.
    Q_PROPERTY(QVariantMap currentLocation READ currentLocation NOTIFY metadataChanged)
    Q_PROPERTY(bool locating READ locating NOTIFY locationStateChanged)
    Q_PROPERTY(QString locationError READ locationError NOTIFY locationStateChanged)
    // Every tag and how many entries carry it, most used first.
    Q_PROPERTY(QVariantList tagCounts READ tagCounts NOTIFY tagCountsChanged)
    // How many entries each day (yyyy-MM-dd) has, for the calendar.
    Q_PROPERTY(QVariantMap entryDays READ entryDays NOTIFY entryDaysChanged)
    // Only changes when another entry opens; typing goes the other way.
    Q_PROPERTY(QString currentText READ currentText NOTIFY currentChanged)
    Q_PROPERTY(SaveState saveState READ saveState NOTIFY saveStateChanged)
    Q_PROPERTY(QString saveError READ saveError NOTIFY saveStateChanged)

public:
    enum SaveState { Saved, Unsaved, Failed };
    Q_ENUM(SaveState)

    // Tests pass their own location source; by default it's the desktop's.
    explicit Backend(Vault *vault, LocationSource *location = nullptr, QObject *parent = nullptr);
    ~Backend() override;

    EntryModel *entries() { return &m_entries; }
    SearchModel *search() { return &m_search; }
    bool loading() const { return m_load.isRunning(); }
    QStringList loadErrors() const { return m_loadErrors; }
    QString currentId() const { return m_current.id; }
    int currentIndex() const { return m_entries.indexOf(m_current.id); }
    QDateTime currentCreated() const { return m_current.created; }
    QString currentDay() const { return m_current.created.date().toString(Qt::ISODate); }
    QString currentTime() const { return m_current.created.time().toString("HH:mm"); }
    QStringList currentTags() const { return m_current.tags; }
    QStringList currentInlineTags() const { return m_inlineTags; }
    QVariantMap currentLocation() const;
    bool locating() const { return !m_locatingFor.isEmpty(); }
    QString locationError() const { return m_locationError; }
    QVariantList tagCounts() const { return m_tagCounts; }
    QVariantMap entryDays() const { return m_entryDays; }
    QString currentText() const { return m_current.body; }
    SaveState saveState() const { return m_saveState; }
    QString saveError() const { return m_saveError; }

    // Starts an entry dated now. It is saved once it has text.
    Q_INVOKABLE void newEntry();
    // False when the open entry couldn't be saved, which keeps it open.
    Q_INVOKABLE bool openEntry(const QString &id);
    // Opens the newest entry of a day (yyyy-MM-dd); false if it has none.
    Q_INVOKABLE bool openDay(const QString &day);
    Q_INVOKABLE void setText(const QString &text);
    // Adds a tag to the open entry; false if the text can't be a tag.
    Q_INVOKABLE bool addTag(const QString &tag);
    Q_INVOKABLE void removeTag(const QString &tag);
    // Moves the open entry to another date (yyyy-MM-dd) and time (HH:mm),
    // keeping the UTC offset it was written with; false if they're not valid.
    Q_INVOKABLE bool setCreated(const QString &day, const QString &time);
    // Asks where the computer is, for the open entry.
    Q_INVOKABLE void locate();
    Q_INVOKABLE void clearLocation();
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
    void entryDaysChanged();
    void metadataChanged();
    void locationStateChanged();
    void tagCountsChanged();

private:
    void load();
    // Saves the open entry and lets it go; false if it couldn't be saved.
    bool leaveCurrent();
    void setCurrent(const Entry &entry, bool isNew);
    void setSaveState(SaveState state, const QString &error = {});
    // After any change to the open entry: the timeline, what's unsaved, and
    // the autosave.
    void edited();
    void refreshInlineTags();

    Vault *m_vault;
    EntryModel m_entries;
    SearchModel m_search;
    QFutureWatcher<EntryStore::LoadResult> m_load;
    int m_loadGeneration = 0;
    QStringList m_loadErrors;
    Entry m_current;
    // The open entry as it was last saved, to tell what's unsaved.
    Entry m_saved;
    QStringList m_inlineTags;
    LocationSource *m_location;
    QString m_locatingFor;
    QString m_locationError;
    QVariantList m_tagCounts;
    bool m_currentIsNew = false;
    // Set while one entry gives way to another, since the timeline follows
    // the rows that come and go meanwhile.
    bool m_switching = false;
    SaveState m_saveState = Saved;
    QString m_saveError;
    QTimer m_autosave;
    QVariantMap m_entryDays;
};
