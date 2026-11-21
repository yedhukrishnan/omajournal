#include "cli.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimeZone>
#include <algorithm>
#include <csignal>
#include <cstdio>
#include <fcntl.h>
#include <functional>
#include <memory>
#include <optional>
#include <termios.h>
#include <unistd.h>

#include "entrymodel.h"
#include "entrystore.h"
#include "searchmodel.h"
#include "textfold.h"
#include "vault.h"

// What the commands print is plain English rather than tr(), since scripts
// and agents read it. Vault's own messages stay translated, as in the window.

namespace {

void print(FILE *stream, const QString &text) { std::fprintf(stream, "%s\n", text.toUtf8().constData()); }
void print(const QJsonObject &object) { std::fputs(QJsonDocument(object).toJson().constData(), stdout); }
int fail(const QString &message) {
    print(stderr, message);
    return 1;
}

// --- Passphrases -----------------------------------------------------------

// The terminal's settings while echo is off, so an interrupt can restore them.
int ttyFd = -1;
termios savedTty;
bool echoOff = false;

void restoreTty() {
    if (echoOff) {
        tcsetattr(ttyFd, TCSAFLUSH, &savedTty);
        echoOff = false;
    }
}

void interrupted(int signal) {
    restoreTty();
    if (ttyFd >= 0)
        (void)!write(ttyFd, "\n", 1);
    _exit(128 + signal);
}

// Asks on the terminal itself, not stdin or stdout, so either can be a pipe.
std::optional<QString> askOnTty(const QString &prompt, QString *error) {
    ttyFd = open("/dev/tty", O_RDWR | O_CLOEXEC);
    if (ttyFd < 0) {
        *error = "No terminal to ask for the passphrase on. Give it with --passphrase-fd.";
        return std::nullopt;
    }
    const QByteArray promptBytes = prompt.toUtf8();
    (void)!write(ttyFd, promptBytes.constData(), promptBytes.size());
    if (tcgetattr(ttyFd, &savedTty) == 0) {
        termios quiet = savedTty;
        quiet.c_lflag &= ~tcflag_t(ECHO);
        quiet.c_lflag |= ECHONL;
        std::signal(SIGINT, interrupted);
        std::signal(SIGTERM, interrupted);
        std::signal(SIGHUP, interrupted);
        echoOff = tcsetattr(ttyFd, TCSAFLUSH, &quiet) == 0;
    }
    QByteArray line;
    char c;
    ssize_t got;
    while ((got = read(ttyFd, &c, 1)) == 1 && c != '\n')
        line += c;
    restoreTty();
    std::signal(SIGINT, SIG_DFL);
    std::signal(SIGTERM, SIG_DFL);
    std::signal(SIGHUP, SIG_DFL);
    close(ttyFd);
    ttyFd = -1;
    if (got != 1 && line.isEmpty()) {
        *error = "No passphrase given.";
        return std::nullopt;
    }
    const QString text = QString::fromUtf8(line);
    Crypto::wipe(line);
    return text;
}

// The passphrases a command needs, one per line of --passphrase-fd, or asked
// for one at a time on the terminal.
struct Passphrases {
    QStringList values;
    QString error;
};

Passphrases readPassphrases(const QCommandLineParser &parser, const QStringList &prompts) {
    Passphrases result;
    if (parser.isSet("passphrase-fd")) {
        bool ok = false;
        const int fd = parser.value("passphrase-fd").toInt(&ok);
        QFile input;
        if (!ok || fd < 0 || !input.open(fd, QIODevice::ReadOnly)) {
            result.error = "Couldn't read from --passphrase-fd " + parser.value("passphrase-fd") + ".";
            return result;
        }
        for (int i = 0; i < prompts.size(); ++i) {
            QByteArray line = input.readLine();
            if (line.endsWith('\n'))
                line.chop(1);
            if (line.endsWith('\r'))
                line.chop(1);
            if (line.isEmpty()) {
                result.error = prompts.size() == 1
                                   ? QString("No passphrase on --passphrase-fd.")
                                   : QString("--passphrase-fd needs %1 lines.").arg(prompts.size());
                result.values.clear();
                return result;
            }
            result.values << QString::fromUtf8(line);
            Crypto::wipe(line);
        }
        return result;
    }
    for (const QString &prompt : prompts) {
        auto value = askOnTty(prompt, &result.error);
        if (!value) {
            result.values.clear();
            return result;
        }
        result.values << *value;
    }
    return result;
}

// --- The journal -----------------------------------------------------------

// Starts unlock or changePassphrase and waits for the vault's answer; empty
// when it worked. Listening starts first, since some failures come at once.
QString waitFor(Vault &vault, const std::function<void()> &start) {
    QString error;
    QEventLoop loop;
    QObject::connect(&vault, &Vault::unlocked, &loop, &QEventLoop::quit);
    QObject::connect(&vault, &Vault::failed, &loop, [&](const QString &message) {
        error = message;
        loop.quit();
    });
    start();
    if (vault.busy())
        loop.exec();
    return error;
}

// Argon2id's limits as the key file has them, so a new passphrase is made as
// hard to guess as the journal's current one.
Crypto::KdfLimits limitsOf(const QString &keyFile) {
    QFile file(keyFile);
    Crypto::KdfLimits limits = Crypto::KdfLimits::moderate();
    if (!file.open(QIODevice::ReadOnly))
        return limits;
    const QJsonObject kdf = QJsonDocument::fromJson(file.readAll()).object().value("kdf").toObject();
    if (kdf["opslimit"].toDouble() > 0 && kdf["memlimit"].toDouble() > 0)
        limits = {static_cast<unsigned long long>(kdf["opslimit"].toDouble()),
                  static_cast<std::size_t>(kdf["memlimit"].toDouble())};
    return limits;
}

// --journal, relative to the current folder unless it starts with ~, or else
// the folder the app uses.
QString journalPath(const QCommandLineParser &parser) {
    if (!parser.isSet("journal"))
        return Vault().path();
    const QString path = parser.value("journal").trimmed();
    return path.startsWith('~') ? path : QDir::current().absoluteFilePath(path);
}

struct Command {
    QCommandLineParser parser;

    Command(const QString &name, const QString &syntax, const QString &description) {
        parser.setApplicationDescription(description);
        parser.addHelpOption();
        parser.addPositionalArgument(name, description, name + (syntax.isEmpty() ? "" : " " + syntax));
    }
    void journalOptions() {
        parser.addOption({"journal", "The journal's folder, instead of the one the app uses.", "dir"});
        parser.addOption({"passphrase-fd", "Read the passphrase from this file descriptor, not the terminal.", "n"});
    }
    void json() { parser.addOption({"json", "Print the result as JSON."}); }
    bool isJson() const { return parser.isSet("json"); }
    QStringList arguments() const { return parser.positionalArguments().mid(1); }
};

// A journal opened for a command: the vault, unlocked, and its entries.
struct OpenJournal {
    std::unique_ptr<Vault> vault;
    QList<Entry> entries;
    QString error;
};

// Opens the journal the command names, asking for its passphrase.
OpenJournal openJournal(const QCommandLineParser &parser, bool loadEntries = true) {
    OpenJournal journal;
    const QString path = journalPath(parser);
    journal.vault = std::make_unique<Vault>(QString(), path);
    journal.vault->setPath(path);
    if (!journal.vault->exists()) {
        journal.error = "No journal in " + journal.vault->path() + ". Run omajournal open to make one.";
        return journal;
    }
    const Passphrases passphrase = readPassphrases(parser, {"Passphrase: "});
    if (!passphrase.error.isEmpty()) {
        journal.error = passphrase.error;
        return journal;
    }
    const QString secret = passphrase.values.first();
    journal.error = waitFor(*journal.vault, [&] { journal.vault->unlock(secret); });
    if (!journal.error.isEmpty() || !loadEntries)
        return journal;
    EntryStore::LoadResult loaded = EntryStore::loadAll(journal.vault->entriesPath(), *journal.vault->key());
    // An unreadable file is reported, not hidden, but the rest still load.
    for (const QString &problem : loaded.errors)
        print(stderr, "Skipped " + problem);
    journal.entries = std::move(loaded.entries);
    std::sort(journal.entries.begin(), journal.entries.end(), [](const Entry &a, const Entry &b) {
        return a.created != b.created ? a.created > b.created : a.id < b.id;
    });
    return journal;
}

// --- Output ----------------------------------------------------------------

QString shortId(const QString &id) { return id.left(8); }

QString when(const Entry &entry) { return entry.created.toString("yyyy-MM-dd HH:mm"); }

QJsonValue locationJson(const Location &location) {
    if (!location.valid)
        return QJsonValue::Null;
    QJsonObject object{{"latitude", location.latitude}, {"longitude", location.longitude}};
    if (location.accuracy > 0)
        object["accuracy"] = location.accuracy;
    return object;
}

QJsonObject summaryJson(const Entry &entry) {
    return {{"id", entry.id},
            {"created", entry.created.toString(Qt::ISODate)},
            {"title", EntryModel::title(entry.body)},
            {"tags", QJsonArray::fromStringList(entry.allTags())}};
}

QString summaryLine(const Entry &entry) {
    const QString title = EntryModel::title(entry.body);
    QString line = shortId(entry.id) + "  " + when(entry) + "  " + (title.isEmpty() ? "(empty)" : title);
    const QStringList tags = entry.allTags();
    if (!tags.isEmpty())
        line += "  #" + tags.join(" #");
    return line;
}

// A snippet from the search model as plain text: no bold, no entities.
QString plainSnippet(const QString &html) {
    QString text = html;
    text.remove("<b>").remove("</b>");
    text.replace("&lt;", "<").replace("&gt;", ">").replace("&quot;", "\"").replace("&#39;", "'");
    return text.replace("&amp;", "&");
}

// A tag given on the command line, with an error when it can't be one.
std::optional<QString> tagOption(const QCommandLineParser &parser, QString *error) {
    if (!parser.isSet("tag"))
        return QString();
    const QString tag = EntryStore::normalizeTag(parser.value("tag"));
    if (tag.isEmpty())
        *error = "\"" + parser.value("tag") + "\" can't be a tag: use letters, digits, _ and -.";
    return tag.isEmpty() ? std::nullopt : std::optional<QString>(tag);
}

// A time with the UTC offset it had where it is, to the second, as the app
// stores it.
QDateTime fixedOffset(QDateTime time) {
    time.setTime(QTime(time.time().hour(), time.time().minute(), time.time().second()));
    return time.toTimeZone(QTimeZone::fromSecondsAheadOfUtc(time.offsetFromUtc()));
}

// --- Commands --------------------------------------------------------------

int newEntry(const QStringList &arguments) {
    Command command("new", "[options]", "Write a new entry, from --text or from stdin.");
    command.journalOptions();
    command.json();
    command.parser.addOption({"text", "The entry's Markdown.", "text"});
    command.parser.addOption({"tag", "Tag the entry; repeat for more tags.", "tag"});
    command.parser.addOption({"date", "The day it's for, instead of today.", "yyyy-mm-dd"});
    command.parser.addOption({"time", "The time it's for, instead of now.", "hh:mm"});
    command.parser.process(arguments);
    if (!command.arguments().isEmpty())
        return fail("new takes no arguments; give the text with --text or on stdin.");

    Entry entry;
    entry.id = EntryStore::newId();
    entry.created = EntryStore::now();
    for (const QString &value : command.parser.values("tag")) {
        const QString tag = EntryStore::normalizeTag(value);
        if (tag.isEmpty())
            return fail("\"" + value + "\" can't be a tag: use letters, digits, _ and -.");
        if (!entry.tags.contains(tag))
            entry.tags << tag;
    }
    if (command.parser.isSet("date") || command.parser.isSet("time")) {
        QDate date = entry.created.date();
        QTime time = entry.created.time();
        if (command.parser.isSet("date")) {
            date = QDate::fromString(command.parser.value("date"), "yyyy-MM-dd");
            if (!date.isValid())
                return fail("Give --date as YYYY-MM-DD.");
        }
        if (command.parser.isSet("time")) {
            time = QTime::fromString(command.parser.value("time"), "HH:mm");
            if (!time.isValid())
                return fail("Give --time as HH:MM, from 00:00 to 23:59.");
        }
        entry.created = fixedOffset(QDateTime(date, time));
    }
    const bool fromStdin = !command.parser.isSet("text");
    if (fromStdin && command.parser.isSet("passphrase-fd") && command.parser.value("passphrase-fd") == "0")
        return fail("With --passphrase-fd 0, give the entry with --text: stdin holds the passphrase.");

    // The passphrase comes first, so a wrong one fails before the writing.
    OpenJournal journal = openJournal(command.parser, false);
    if (!journal.error.isEmpty())
        return fail(journal.error);

    if (fromStdin) {
        if (isatty(STDIN_FILENO))
            print(stderr, "Write the entry, then press Ctrl+D on a line of its own.");
        QFile input;
        if (!input.open(stdin, QIODevice::ReadOnly))
            return fail("Couldn't read stdin.");
        entry.body = QString::fromUtf8(input.readAll());
    } else {
        entry.body = command.parser.value("text");
    }
    entry.body.replace("\r\n", "\n");
    if (entry.isBlank())
        return fail("Nothing to write: the entry is empty.");

    QString error;
    if (!EntryStore::save(journal.vault->entriesPath(), *journal.vault->key(), entry, &error))
        return fail("Couldn't save the entry: " + error);
    if (command.isJson())
        print(QJsonObject{{"entry", summaryJson(entry)}});
    else
        print(stdout, entry.id);
    return 0;
}

int list(const QStringList &arguments) {
    Command command("list", "[options]", "List the entries, newest first.");
    command.journalOptions();
    command.json();
    command.parser.addOption({"month", "Only entries from this month.", "yyyy-mm"});
    command.parser.addOption({"tag", "Only entries with this tag.", "tag"});
    command.parser.process(arguments);
    if (!command.arguments().isEmpty())
        return fail("list takes no arguments.");
    QString month;
    if (command.parser.isSet("month")) {
        const QDate date = QDate::fromString(command.parser.value("month") + "-01", "yyyy-MM-dd");
        if (!date.isValid())
            return fail("Give --month as YYYY-MM.");
        month = date.toString("yyyy-MM");
    }
    QString error;
    const auto tag = tagOption(command.parser, &error);
    if (!tag)
        return fail(error);

    OpenJournal journal = openJournal(command.parser);
    if (!journal.error.isEmpty())
        return fail(journal.error);
    QJsonArray entries;
    for (const Entry &entry : journal.entries) {
        if (!month.isEmpty() && entry.created.toString("yyyy-MM") != month)
            continue;
        if (!tag->isEmpty() && !entry.allTags().contains(*tag))
            continue;
        if (command.isJson())
            entries << summaryJson(entry);
        else
            print(stdout, summaryLine(entry));
    }
    if (command.isJson())
        print(QJsonObject{{"entries", entries}});
    return 0;
}

int search(const QStringList &arguments) {
    Command command("search", "<words>... [options]",
                    "Find the entries holding every word, whatever their case or accents.");
    command.journalOptions();
    command.json();
    command.parser.addOption({"tag", "Only entries with this tag.", "tag"});
    command.parser.process(arguments);
    const QStringList terms = TextFold::terms(command.arguments().join(' '));
    if (terms.isEmpty())
        return fail("Give the words to search for.");
    QString error;
    const auto tag = tagOption(command.parser, &error);
    if (!tag)
        return fail(error);

    OpenJournal journal = openJournal(command.parser);
    if (!journal.error.isEmpty())
        return fail(journal.error);
    QJsonArray results;
    for (const Entry &entry : journal.entries) {
        if (!tag->isEmpty() && !entry.allTags().contains(*tag))
            continue;
        const QString folded = TextFold::fold(entry.body);
        if (!std::all_of(terms.begin(), terms.end(), [&](const QString &term) { return folded.contains(term); }))
            continue;
        const QString snippet = plainSnippet(SearchModel::snippet(entry.body, terms));
        if (command.isJson()) {
            QJsonObject result = summaryJson(entry);
            result["snippet"] = snippet;
            results << result;
        } else {
            print(stdout, summaryLine(entry));
            if (!snippet.isEmpty())
                print(stdout, "    " + snippet);
        }
    }
    if (command.isJson())
        print(QJsonObject{{"entries", results}});
    return 0;
}

// The entry an id or the start of one names, or an error.
const Entry *findEntry(const QList<Entry> &entries, const QString &id, QString *error) {
    const QString wanted = id.trimmed().toLower();
    const Entry *found = nullptr;
    for (const Entry &entry : entries) {
        if (entry.id == wanted)
            return &entry;
        if (!entry.id.startsWith(wanted))
            continue;
        if (found) {
            *error = "More than one entry starts with " + id + "; give more of the id.";
            return nullptr;
        }
        found = &entry;
    }
    if (!found)
        *error = "No entry has the id " + id + ".";
    return found;
}

int show(const QStringList &arguments) {
    Command command("show", "<id> [options]", "Print an entry: its front matter, then its Markdown.");
    command.journalOptions();
    command.json();
    command.parser.process(arguments);
    if (command.arguments().size() != 1 || command.arguments().first().trimmed().isEmpty())
        return fail("Give one entry's id, or the start of it.");

    OpenJournal journal = openJournal(command.parser);
    if (!journal.error.isEmpty())
        return fail(journal.error);
    QString error;
    const Entry *entry = findEntry(journal.entries, command.arguments().first(), &error);
    if (!entry)
        return fail(error);
    if (command.isJson()) {
        QJsonObject object = summaryJson(*entry);
        object["location"] = locationJson(entry->location);
        object["body"] = entry->body;
        print(QJsonObject{{"entry", object}});
    } else {
        QByteArray text = EntryStore::serialize(*entry);
        std::fwrite(text.constData(), 1, size_t(text.size()), stdout);
        if (!text.endsWith('\n'))
            std::fputc('\n', stdout);
        Crypto::wipe(text);
    }
    return 0;
}

// Writes bytes to a new file only the owner can read.
bool writePrivate(const QString &path, const QByteArray &bytes, QString *error) {
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)
        || file.write(bytes) != bytes.size() || !file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

int exportEntries(const QStringList &arguments) {
    Command command("export", "--decrypt <dir> [options]",
                    "Write every entry, decrypted, as Markdown files in a new or empty folder.");
    command.journalOptions();
    command.json();
    command.parser.addOption({"decrypt", "Say that the files may be unencrypted. Required."});
    command.parser.process(arguments);
    if (!command.parser.isSet("decrypt"))
        return fail("export writes entries unencrypted; add --decrypt to say that's what you want.");
    if (command.arguments().size() != 1)
        return fail("Give the folder to export to.");
    const QString directory = QDir::cleanPath(QDir::current().absoluteFilePath(command.arguments().first()));
    const QFileInfo info(directory);
    if (info.exists() && !info.isDir())
        return fail(directory + " is a file, not a folder.");
    if (info.exists() && !QDir(directory).isEmpty(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot))
        return fail(directory + " isn't empty. Export to a new or empty folder.");

    OpenJournal journal = openJournal(command.parser);
    if (!journal.error.isEmpty())
        return fail(journal.error);
    if (!QDir().mkpath(directory))
        return fail("Couldn't make the folder " + directory + ".");
    QFile::setPermissions(directory, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

    QJsonArray files;
    QStringList names;
    for (const Entry &entry : journal.entries) {
        // Copies a sync tool made share the id's start, so a name can repeat.
        const QString base = entry.created.toString("yyyy-MM-dd-HHmm") + "-" + shortId(entry.id);
        QString name = base + ".md";
        for (int n = 2; names.contains(name); ++n)
            name = base + "-" + QString::number(n) + ".md";
        names << name;
        QByteArray text = EntryStore::serialize(entry);
        QString error;
        const bool written = writePrivate(directory + "/" + name, text, &error);
        Crypto::wipe(text);
        if (!written)
            return fail("Couldn't write " + name + ": " + error);
        files << QJsonObject{{"id", entry.id}, {"file", name}};
    }
    print(stderr, "Warning: anyone who can read " + directory
                      + " can read these entries. Delete them when you're done.");
    if (command.isJson())
        print(QJsonObject{{"directory", directory}, {"files", files}});
    else
        print(stdout, QString("Exported %1 entries to %2").arg(files.size()).arg(directory));
    return 0;
}

int importEntries(const QStringList &arguments) {
    Command command("import", "<dir> [options]",
                    "Add every .md file in a folder as a new entry. Files without front matter are dated "
                    "by when they were last changed.");
    command.journalOptions();
    command.json();
    command.parser.process(arguments);
    if (command.arguments().size() != 1)
        return fail("Give the folder to import from.");
    const QDir directory(command.arguments().first());
    if (!directory.exists())
        return fail("No folder " + command.arguments().first() + ".");

    // Every file is read before anything is saved, so a bad one stops the
    // import without leaving half of it in the journal.
    QList<QPair<QString, Entry>> found;
    for (const QFileInfo &info : directory.entryInfoList({"*.md"}, QDir::Files, QDir::Name)) {
        QFile file(info.filePath());
        if (!file.open(QIODevice::ReadOnly))
            return fail("Couldn't read " + info.fileName() + ": " + file.errorString());
        QByteArray text = file.readAll();
        text.replace("\r\n", "\n");
        const QString id = EntryStore::newId();
        std::optional<Entry> entry;
        if (text.startsWith("---\n")) {
            QString error;
            entry = EntryStore::parse(id, text, &error);
            if (!entry)
                return fail(info.fileName() + ": " + error);
        } else {
            entry = Entry(id, fixedOffset(info.lastModified()), QString::fromUtf8(text));
        }
        Crypto::wipe(text);
        found << QPair<QString, Entry>{info.fileName(), *entry};
    }
    if (found.isEmpty())
        return fail("No .md files in " + command.arguments().first() + ".");

    OpenJournal journal = openJournal(command.parser, false);
    if (!journal.error.isEmpty())
        return fail(journal.error);
    QJsonArray imported;
    for (const auto &[name, entry] : found) {
        QString error;
        if (!EntryStore::save(journal.vault->entriesPath(), *journal.vault->key(), entry, &error))
            return fail(QString("Couldn't save %1: %2 (%3 of %4 were imported)")
                            .arg(name, error).arg(imported.size()).arg(found.size()));
        imported << QJsonObject{{"file", name}, {"id", entry.id}};
    }
    if (command.isJson())
        print(QJsonObject{{"entries", imported}});
    else
        print(stdout, QString("Imported %1 entries").arg(imported.size()));
    return 0;
}

int passwd(const QStringList &arguments) {
    Command command("passwd", "[options]",
                    "Change the passphrase. With --passphrase-fd, the first line is the current passphrase "
                    "and the second the new one.");
    command.journalOptions();
    command.parser.process(arguments);
    if (!command.arguments().isEmpty())
        return fail("passwd takes no arguments.");
    const QString path = journalPath(command.parser);
    Vault probe(QString(), path);
    probe.setPath(path);
    if (!probe.exists())
        return fail("No journal in " + probe.path() + ". Run omajournal open to make one.");

    const bool fromFd = command.parser.isSet("passphrase-fd");
    const Passphrases given = readPassphrases(
        command.parser, fromFd ? QStringList{"", ""}
                               : QStringList{"Current passphrase: ", "New passphrase: ", "New passphrase again: "});
    if (!given.error.isEmpty())
        return fail(given.error);
    if (!fromFd && given.values[1] != given.values[2])
        return fail("The new passphrases don't match.");
    if (Vault::passphraseStrength(given.values[1]) == 0)
        return fail("The new passphrase needs at least 8 characters.");

    Vault vault(QString(), path, limitsOf(probe.keyFile()));
    vault.setPath(path);
    const QString error = waitFor(vault, [&] { vault.changePassphrase(given.values[0], given.values[1]); });
    if (!error.isEmpty())
        return fail(error);
    print(stdout, "Changed the passphrase. The recovery key still works.");
    return 0;
}

int help(const QStringList &arguments) {
    const QString topic = arguments.value(2);
    if (Cli::isCommand(topic) && topic != "help" && !topic.startsWith('-'))
        return Cli::run({arguments[0], topic, "--help"});
    print(stdout, "Usage: omajournal <command> [options]\n\n" + Cli::summary());
    return 0;
}

int openHelp(const QStringList &arguments) {
    Command command("open", "", "Open the journal's window.");
    command.parser.addVersionOption();
    command.parser.process(arguments);
    return 0;
}

} // namespace

namespace Cli {

bool isCommand(const QString &word) {
    return QStringList{"new", "list", "search", "show", "export", "import", "passwd", "help", "--help", "-h",
                       "--version", "-v"}
        .contains(word);
}

QString summary() {
    return "Window:\n"
           "  open                      Open the journal\n\n"
           "Commands that need no display (omajournal help <command> for options):\n"
           "  new                       Write an entry, from --text or stdin\n"
           "  list                      List the entries, newest first\n"
           "  search <words>...         Find entries holding every word\n"
           "  show <id>                 Print an entry\n"
           "  export --decrypt <dir>    Write every entry, decrypted, into a folder\n"
           "  import <dir>              Add the .md files in a folder as entries\n"
           "  passwd                    Change the passphrase\n\n"
           "Each command takes --journal <dir> for another journal, and --passphrase-fd <n> to read the\n"
           "passphrase from a file descriptor instead of the terminal. Most take --json.";
}

int run(const QStringList &arguments) {
    const QString command = arguments.value(1);
    if (command == "--version" || command == "-v") {
        print(stdout, QCoreApplication::applicationName() + " " + QCoreApplication::applicationVersion());
        return 0;
    }
    if (command.isEmpty() || command == "help" || command == "--help" || command == "-h")
        return help(arguments);
    if (command == "open")
        return openHelp(arguments);
    if (!isCommand(command)) {
        print(stderr, "omajournal: no command " + command + ".\n");
        print(stderr, "Usage: omajournal <command> [options]\n\n" + summary());
        return 1;
    }
    return command == "new"      ? newEntry(arguments)
           : command == "list"   ? list(arguments)
           : command == "search" ? search(arguments)
           : command == "show"   ? show(arguments)
           : command == "export" ? exportEntries(arguments)
           : command == "import" ? importEntries(arguments)
                                 : passwd(arguments);
}

} // namespace Cli
