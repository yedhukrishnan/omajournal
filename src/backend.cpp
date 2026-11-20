#include "backend.h"

#include <QQmlEngine>
#include <QQuickTextDocument>
#include <QTextDocument>
#include <QtConcurrent>

#include "location.h"
#include "markdownhighlighter.h"
#include "vault.h"

Backend::Backend(Vault *vault, LocationSource *location, QObject *parent)
    : QObject(parent), m_vault(vault), m_location(location ? location : new PortalLocation(this)) {
    connect(m_location, &LocationSource::located, this, [this](double latitude, double longitude, double accuracy) {
        const QString id = m_locatingFor;
        m_locatingFor.clear();
        m_locationError.clear();
        emit locationStateChanged();
        // Only for the entry it was asked for, if that's still open.
        if (id.isEmpty() || id != m_current.id)
            return;
        m_current.location = {latitude, longitude, accuracy, true};
        emit metadataChanged();
        edited();
    });
    connect(m_location, &LocationSource::failed, this, [this](const QString &message) {
        m_locatingFor.clear();
        m_locationError = message;
        emit locationStateChanged();
    });

    m_search.setSourceModel(&m_entries);
    m_autosave.setSingleShot(true);
    m_autosave.setInterval(800);
    connect(&m_autosave, &QTimer::timeout, this, &Backend::save);
    connect(m_vault, &Vault::unlocked, this, &Backend::load);
    connect(m_vault, &Vault::created, this, &Backend::load);

    // Days only change as entries come and go; a title changing leaves them be.
    const auto refreshDays = [this] {
        const QVariantMap days = m_entries.entryDays();
        if (days != m_entryDays) {
            m_entryDays = days;
            emit entryDaysChanged();
        }
    };
    connect(&m_entries, &QAbstractItemModel::rowsInserted, this, refreshDays);
    connect(&m_entries, &QAbstractItemModel::rowsRemoved, this, refreshDays);
    connect(&m_entries, &QAbstractItemModel::modelReset, this, refreshDays);

    // Tags change with what's written too.
    const auto refreshTags = [this] {
        const QVariantList counts = m_entries.tagCounts();
        if (counts != m_tagCounts) {
            m_tagCounts = counts;
            emit tagCountsChanged();
        }
    };
    connect(&m_entries, &QAbstractItemModel::rowsInserted, this, refreshTags);
    connect(&m_entries, &QAbstractItemModel::rowsRemoved, this, refreshTags);
    connect(&m_entries, &QAbstractItemModel::modelReset, this, refreshTags);
    connect(&m_entries, &QAbstractItemModel::dataChanged, this, refreshTags);

    connect(&m_load, &QFutureWatcher<EntryStore::LoadResult>::started, this, &Backend::loadingChanged);
    connect(&m_load, &QFutureWatcher<EntryStore::LoadResult>::finished, this, [this] {
        const int generation = m_load.property("generation").toInt();
        if (generation != m_loadGeneration || m_vault->locked()) {
            emit loadingChanged();
            return;
        }
        EntryStore::LoadResult result = m_load.result();
        m_loadErrors = result.errors;
        m_entries.setEntries(std::move(result.entries));
        emit loadingChanged();
        // The newest entry opens, so the window starts where you left off.
        if (m_current.id.isEmpty() && m_entries.count() > 0)
            setCurrent(*m_entries.find(m_entries.idAt(0)), false);
        else
            emit currentIndexChanged();
    });
}

Backend::~Backend() { m_load.waitForFinished(); }

void Backend::load() {
    const auto key = m_vault->key();
    if (!key)
        return;
    const QString directory = m_vault->entriesPath();
    m_load.setProperty("generation", ++m_loadGeneration);
    m_load.setFuture(QtConcurrent::run([key, directory] { return EntryStore::loadAll(directory, *key); }));
}

void Backend::newEntry() {
    // A blank new entry is already a fresh page.
    if (m_currentIsNew && m_current.isBlank()) {
        emit currentChanged();
        return;
    }
    m_switching = true;
    const bool left = leaveCurrent();
    m_switching = false;
    if (!left)
        return;
    const Entry entry(EntryStore::newId(), EntryStore::now(), {});
    m_entries.upsert(entry);
    setCurrent(entry, true);
}

bool Backend::openEntry(const QString &id) {
    if (id == m_current.id || m_switching)
        return true;
    const Entry *found = m_entries.find(id);
    if (!found)
        return false;
    const Entry entry = *found;
    m_switching = true;
    const bool left = leaveCurrent();
    m_switching = false;
    if (!left) {
        emit currentIndexChanged();
        return false;
    }
    setCurrent(entry, false);
    return true;
}

void Backend::registerQmlTypes() {
    qmlRegisterUncreatableType<Backend>("Omajournal", 1, 0, "Backend", "The backend comes from C++.");
    qmlRegisterType<MarkdownHighlighter>("Omajournal", 1, 0, "MarkdownHighlighter");
    qmlRegisterUncreatableType<SearchModel>("Omajournal", 1, 0, "SearchModel", "The search comes from the backend.");
}

bool Backend::openDay(const QString &day) {
    const int row = m_entries.firstRowOnDay(day);
    return row >= 0 && openEntry(m_entries.idAt(row));
}

void Backend::setText(const QString &text) {
    if (m_current.id.isEmpty() || text == m_current.body)
        return;
    m_current.body = text;
    refreshInlineTags();
    edited();
}

void Backend::edited() {
    const int row = m_entries.indexOf(m_current.id);
    m_entries.upsert(m_current);
    if (m_entries.indexOf(m_current.id) != row)
        emit currentIndexChanged();
    // Unsaved is decided by the content, not by counting edits, so undoing
    // back to what was saved is clean again.
    if (!(m_current == m_saved)) {
        setSaveState(Unsaved);
        m_autosave.start();
    } else {
        m_autosave.stop();
        setSaveState(Saved);
    }
}

void Backend::refreshInlineTags() {
    const QStringList tags = EntryStore::inlineTags(m_current.body);
    if (tags != m_inlineTags) {
        m_inlineTags = tags;
        emit metadataChanged();
    }
}

bool Backend::addTag(const QString &text) {
    const QString tag = EntryStore::normalizeTag(text);
    if (m_current.id.isEmpty() || tag.isEmpty())
        return false;
    if (!m_current.tags.contains(tag)) {
        m_current.tags << tag;
        emit metadataChanged();
        edited();
    }
    return true;
}

void Backend::removeTag(const QString &tag) {
    if (m_current.tags.removeAll(tag) > 0) {
        emit metadataChanged();
        edited();
    }
}

bool Backend::setCreated(const QString &day, const QString &time) {
    const QDate date = QDate::fromString(day, Qt::ISODate);
    const QTime clock = QTime::fromString(time, "H:mm");
    if (m_current.id.isEmpty() || !date.isValid() || !clock.isValid())
        return false;
    const QDateTime created(date, clock, m_current.created.timeZone());
    if (!created.isValid())
        return false;
    if (created != m_current.created) {
        m_current.created = created;
        emit metadataChanged();
        edited();
    }
    return true;
}

QVariantMap Backend::currentLocation() const {
    if (!m_current.location.valid)
        return {};
    return {{"latitude", m_current.location.latitude},
            {"longitude", m_current.location.longitude},
            {"accuracy", m_current.location.accuracy}};
}

void Backend::locate() {
    if (m_current.id.isEmpty() || locating())
        return;
    m_locatingFor = m_current.id;
    m_locationError.clear();
    emit locationStateChanged();
    m_location->request();
}

void Backend::clearLocation() {
    if (!m_current.location.valid)
        return;
    m_current.location = {};
    emit metadataChanged();
    edited();
}

QString Backend::textOf(QQuickTextDocument *document) {
    if (!document || !document->textDocument())
        return {};
    // toPlainText() would turn non-breaking spaces into spaces; the raw text
    // keeps them, with only the paragraph separators back to newlines.
    QString text = document->textDocument()->toRawText();
    text.replace(QChar::ParagraphSeparator, '\n');
    return text;
}

void Backend::updateFromDocument(QQuickTextDocument *document) { setText(textOf(document)); }

bool Backend::save() {
    m_autosave.stop();
    if (m_current.id.isEmpty() || m_current == m_saved)
        return true;
    const auto key = m_vault->key();
    QString error;
    if (!key) {
        setSaveState(Failed, tr("The journal is locked."));
        return false;
    }
    if (!EntryStore::save(m_vault->entriesPath(), *key, m_current, &error)) {
        setSaveState(Failed, error);
        return false;
    }
    m_saved = m_current;
    m_currentIsNew = false;
    setSaveState(Saved);
    return true;
}

bool Backend::lock(bool discardUnsaved) {
    if (m_vault->locked())
        return true;
    if (!discardUnsaved && !save())
        return false;
    m_autosave.stop();
    // A load still running is dropped when it finishes.
    ++m_loadGeneration;
    // What was searched for says something about the journal too.
    m_search.setQuery({});
    m_entries.clear();
    m_current = {};
    m_saved = {};
    m_inlineTags.clear();
    m_currentIsNew = false;
    m_loadErrors.clear();
    m_locatingFor.clear();
    m_locationError.clear();
    emit locationStateChanged();
    emit metadataChanged();
    emit currentChanged();
    emit currentIndexChanged();
    emit loadingChanged();
    setSaveState(Saved);
    m_vault->lock();
    return true;
}

bool Backend::leaveCurrent() {
    if (m_current.id.isEmpty())
        return true;
    if (!save())
        return false;
    if (m_currentIsNew && m_current.isBlank())
        m_entries.remove(m_current.id);
    return true;
}

void Backend::setCurrent(const Entry &entry, bool isNew) {
    m_current = entry;
    m_saved = entry;
    m_currentIsNew = isNew;
    m_inlineTags = EntryStore::inlineTags(entry.body);
    if (!m_locationError.isEmpty()) {
        m_locationError.clear();
        emit locationStateChanged();
    }
    setSaveState(Saved);
    emit metadataChanged();
    emit currentChanged();
    emit currentIndexChanged();
}

void Backend::setSaveState(SaveState state, const QString &error) {
    if (state == m_saveState && error == m_saveError)
        return;
    m_saveState = state;
    m_saveError = error;
    emit saveStateChanged();
}
