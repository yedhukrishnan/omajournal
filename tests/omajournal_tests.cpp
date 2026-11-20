#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QtTest>

#include "backend.h"
#include "crypto.h"
#include "entrymodel.h"
#include "entrystore.h"
#include "markdownhighlighter.h"
#include "textfold.h"
#include "vault.h"
#include "theme.h"
#include "translator.h"

namespace {

bool write(const QString &path, const QByteArray &contents) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

int lines(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll().count('\n') : 0;
}

// A hyprctl that answers with response.json beside it and counts its calls.
QString fakeHyprctl(const QString &directory) {
    const QString command = directory + "/hyprctl";
    write(command, "#!/bin/bash\necho >> '" + directory.toUtf8() + "/calls'\ncat '" + directory.toUtf8()
                       + "/response.json'\n");
    QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    return command;
}

// QTest::keyClicks only takes widgets; a window gets one key at a time.
void typeText(QWindow *window, const QString &text) {
    for (const QChar c : text)
        QTest::keyClick(window, c.toLatin1());
}

// The format the highlighter gave a character of a line.
QTextCharFormat formatAt(QTextDocument &document, int line, int column) {
    const QTextBlock block = document.findBlockByNumber(line);
    for (const QTextLayout::FormatRange &range : block.layout()->formats())
        if (column >= range.start && column < range.start + range.length)
            return range.format;
    return {};
}

QJsonObject readCatalog(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
}

// The placeholders in a text, sorted: %1 to %99, and the count (%n, %Ln).
QStringList placeholders(const QString &text) {
    static const QRegularExpression placeholder("%L?(?:[1-9][0-9]?|n)");
    QStringList found;
    for (auto it = placeholder.globalMatch(text); it.hasNext();)
        found << it.next().captured();
    found.sort();
    return found;
}

// Every text the sources hand to qsTr() or tr(), and whether a call passes it
// a count. It walks the code a token at a time, so a string never hides a
// comment or the other way round, C++'s adjacent literals join, and QML's
// single quotes count. A text that isn't a literal can't be in a catalog.
QMap<QString, bool> translatables(const QString &folder, QStringList *problems) {
    QMap<QString, bool> found;
    for (const QFileInfo &info : QDir(folder).entryInfoList({"*.qml", "*.js", "*.cpp", "*.h"}, QDir::Files)) {
        QFile file(info.filePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QString code = QString::fromUtf8(file.readAll());
        qsizetype i = 0;
        auto literal = [&](QString *text) {
            const QChar quote = code[i++];
            for (; i < code.size() && code[i] != quote; ++i) {
                if (code[i] == '\\' && i + 1 < code.size()) {
                    const QChar escaped = code[++i];
                    const int digits = escaped == 'u' ? 4 : escaped == 'x' ? 2 : 0;
                    bool hex = false;
                    const ushort unit = code.mid(i + 1, digits).toUShort(&hex, 16);
                    if (digits && hex) {
                        *text += QChar(unit);
                        i += digits;
                    } else {
                        *text += escaped == 'n' ? QChar('\n') : escaped == 't' ? QChar('\t') : escaped;
                    }
                } else {
                    *text += code[i];
                }
            }
            ++i;
        };
        auto skipSpace = [&] {
            while (i < code.size() && code[i].isSpace())
                ++i;
        };
        while (i < code.size()) {
            const QChar c = code[i];
            if (code.mid(i, 2) == "//") {
                i = code.indexOf('\n', i);
                if (i < 0)
                    break;
            } else if (code.mid(i, 2) == "/*") {
                i = code.indexOf("*/", i + 2);
                if (i < 0)
                    break;
                i += 2;
            } else if (c == '"' || c == '\'') {
                QString skipped;
                literal(&skipped);
            } else if (c.isLetter() || c == '_') {
                const qsizetype start = i;
                while (i < code.size() && (code[i].isLetterOrNumber() || code[i] == '_'))
                    ++i;
                const QString word = code.mid(start, i - start);
                skipSpace();
                if ((word != "qsTr" && word != "tr") || i >= code.size() || code[i] != '(')
                    continue;
                ++i;
                skipSpace();
                if (i >= code.size() || (code[i] != '"' && code[i] != '\'')) {
                    *problems << info.fileName() + ": not a literal: " + code.mid(start, 40);
                    continue;
                }
                QString text;
                do {
                    literal(&text);
                    skipSpace();
                } while (i < code.size() && code[i] == '"');
                // The arguments after the text, to the call's closing parenthesis.
                int depth = 0, commas = 0;
                while (i < code.size()) {
                    const QChar d = code[i];
                    if (d == '"' || d == '\'') {
                        QString skipped;
                        literal(&skipped);
                        continue;
                    }
                    if (d == '(' || d == '[' || d == '{')
                        ++depth;
                    else if ((d == ')' || d == ']' || d == '}') && depth-- == 0)
                        break;
                    else if (d == ',' && depth == 0)
                        ++commas;
                    ++i;
                }
                found[text] = found.value(text) || commas >= 2;
            } else {
                ++i;
            }
        }
    }
    return found;
}

} // namespace

class OmajournalTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(Crypto::initialize());
        QQuickStyle::setStyle("Material");
        Backend::registerQmlTypes();
    }

    // --- Encryption ---------------------------------------------------------

    void filesRoundTrip_data() {
        QTest::addColumn<int>("size");
        for (int size : {0, 1, 100, 65535, 65536, 65537, 200000})
            QTest::newRow(qPrintable(QString::number(size))) << size;
    }

    void filesRoundTrip() {
        QFETCH(int, size);
        const Crypto::SecretKey key = Crypto::SecretKey::random();
        const QByteArray plaintext = Crypto::randomBytes(size);
        const QByteArray file = Crypto::encryptFile(key, plaintext);
        QVERIFY(file.startsWith("OMJ1"));
        const auto decrypted = Crypto::decryptFile(key, file);
        QVERIFY(decrypted);
        QCOMPARE(*decrypted, plaintext);
        // A fresh file id each time, so the same text never encrypts the same.
        QVERIFY(Crypto::encryptFile(key, plaintext) != file);
    }

    void filesDetectTamperingAndTruncation() {
        const Crypto::SecretKey key = Crypto::SecretKey::random();
        const QByteArray file = Crypto::encryptFile(key, Crypto::randomBytes(150000));
        QVERIFY(Crypto::decryptFile(key, file));

        // Any byte changed: the id, the stream header, the first and last chunks.
        for (int at : {5, 20, 60, int(file.size()) - 1}) {
            QByteArray damaged = file;
            damaged[at] = char(damaged[at] ^ 1);
            QVERIFY2(!Crypto::decryptFile(key, damaged), qPrintable(QString::number(at)));
        }
        QString error;
        QVERIFY(!Crypto::decryptFile(key, file.chopped(1), &error));
        // Cut exactly after a whole chunk, so only the missing final tag tells.
        const int firstChunkEnd = 4 + 8 + 24 + 65536 + 17;
        QVERIFY(!Crypto::decryptFile(key, file.left(firstChunkEnd), &error));
        QCOMPARE(error, QString("The file was cut short."));
        QVERIFY(!Crypto::decryptFile(key, file + "x", &error));
        QVERIFY(!Crypto::decryptFile(key, "hello world, not encrypted at all", &error));
        QCOMPARE(error, QString("This isn't an Omajournal file."));
        QVERIFY(!Crypto::decryptFile(Crypto::SecretKey::random(), file));
    }

    void wrappedKeysNeedTheRightKeyAndSlot() {
        const Crypto::SecretKey wrapping = Crypto::SecretKey::random(), secret = Crypto::SecretKey::random();
        const Crypto::Wrapped wrapped = Crypto::wrap(wrapping, secret, "slot a");
        const auto opened = Crypto::unwrap(wrapping, wrapped, "slot a");
        QVERIFY(opened);
        QCOMPARE(QByteArray(reinterpret_cast<const char *>(opened->data()), 32),
                 QByteArray(reinterpret_cast<const char *>(secret.data()), 32));
        QVERIFY(!Crypto::unwrap(wrapping, wrapped, "slot b"));
        QVERIFY(!Crypto::unwrap(Crypto::SecretKey::random(), wrapped, "slot a"));
    }

    void passphrasesDeriveTheSameKeyFromTheSameSalt() {
        const QByteArray salt = Crypto::randomBytes(16);
        const auto limits = Crypto::KdfLimits::minimum();
        const auto a = Crypto::keyFromPassphrase("correct horse", salt, limits);
        const auto b = Crypto::keyFromPassphrase("correct horse", salt, limits);
        const auto c = Crypto::keyFromPassphrase("correct horsf", salt, limits);
        QVERIFY(a && b && c);
        QCOMPARE(QByteArray(reinterpret_cast<const char *>(a->data()), 32),
                 QByteArray(reinterpret_cast<const char *>(b->data()), 32));
        QVERIFY(QByteArray(reinterpret_cast<const char *>(a->data()), 32)
                != QByteArray(reinterpret_cast<const char *>(c->data()), 32));
        QVERIFY(!Crypto::keyFromPassphrase("x", "short salt", limits));
    }

    void recoveryKeysRoundTrip() {
        const QByteArray bytes = Crypto::randomBytes(32);
        const QString key = Crypto::encodeRecoveryKey(bytes);
        QCOMPARE(key.split('-').size(), 13);
        QCOMPARE(QString(key).remove('-').size(), 52);
        const QString typed = Crypto::encodeRecoveryKey(bytes);
        QCOMPARE(Crypto::decodeRecoveryKey(typed), bytes);
        QCOMPARE(Crypto::decodeRecoveryKey(typed.toLower().replace('-', ' ')), bytes);
        QVERIFY(Crypto::decodeRecoveryKey(typed.chopped(5)).isEmpty());
        QVERIFY(Crypto::decodeRecoveryKey(typed + "AAAA").isEmpty());
        QVERIFY(Crypto::decodeRecoveryKey("not a key!").isEmpty());
        QVERIFY(Vault::confirmsRecoveryKey(typed, typed.right(9).toLower()));
        QVERIFY(!Vault::confirmsRecoveryKey(typed, typed.right(4)));
    }

    // --- The vault ----------------------------------------------------------

    void vaultCreatesUnlocksAndRecovers() {
        QTemporaryDir dir;
        const QString journal = dir.filePath("journal");
        Vault vault(dir.filePath("settings.json"), journal, Crypto::KdfLimits::minimum());
        QCOMPARE(vault.path(), journal);
        QVERIFY(!vault.exists());
        QVERIFY(vault.locked());
        QCOMPARE(vault.pathProblem(), QString());

        QSignalSpy created(&vault, &Vault::created), unlocked(&vault, &Vault::unlocked), failed(&vault, &Vault::failed);
        vault.create("correct horse battery");
        QTRY_COMPARE(created.size(), 1);
        QVERIFY(vault.exists());
        QVERIFY(!vault.locked());
        const QString recovery = created.first().first().toString();
        QCOMPARE(Crypto::decodeRecoveryKey(recovery).size(), 32);
        QCOMPARE(QFileInfo(vault.keyFile()).permissions() & (QFile::ReadGroup | QFile::ReadOther), QFileDevice::Permissions());
        QCOMPARE(QFileInfo(journal).permissions() & (QFile::ReadGroup | QFile::ReadOther), QFileDevice::Permissions());

        // Something encrypted now must still open after a recovery.
        const QByteArray sealed = Crypto::encryptFile(*vault.key(), "kept");

        vault.lock();
        QVERIFY(vault.locked());
        QVERIFY(!vault.key());
        vault.unlock("wrong horse");
        QTRY_COMPARE(failed.size(), 1);
        QVERIFY(vault.locked());
        vault.unlock("correct horse battery");
        QTRY_COMPARE(unlocked.size(), 1);
        QCOMPARE(*Crypto::decryptFile(*vault.key(), sealed), QByteArray("kept"));

        vault.lock();
        vault.recover("AAAA-BBBB", "new passphrase");
        QCOMPARE(failed.size(), 2);
        vault.recover(recovery, "new passphrase");
        QTRY_COMPARE(unlocked.size(), 2);
        QCOMPARE(*Crypto::decryptFile(*vault.key(), sealed), QByteArray("kept"));

        vault.lock();
        vault.unlock("correct horse battery");
        QTRY_COMPARE(failed.size(), 3);
        vault.unlock("new passphrase");
        QTRY_COMPARE(unlocked.size(), 3);

        // The folder is remembered for the next start.
        Vault again(dir.filePath("settings.json"), dir.filePath("elsewhere"), Crypto::KdfLimits::minimum());
        QCOMPARE(again.path(), journal);
        QVERIFY(again.exists());
    }

    void vaultWontStartInAFolderWithOtherFiles() {
        QTemporaryDir dir;
        QVERIFY(write(dir.filePath("notes.txt"), "mine"));
        Vault vault(dir.filePath("settings.json"), dir.path(), Crypto::KdfLimits::minimum());
        QVERIFY(!vault.pathProblem().isEmpty());
        vault.setPath("relative/path");
        QVERIFY(!vault.pathProblem().isEmpty());
        vault.setPath(dir.filePath("notes.txt"));
        QVERIFY(!vault.pathProblem().isEmpty());
        vault.setPath(dir.filePath("fresh"));
        QCOMPARE(vault.pathProblem(), QString());
        vault.setPath("~/" + QDir::home().relativeFilePath(dir.filePath("fresh")));
        QCOMPARE(vault.path(), dir.filePath("fresh"));
    }

    void passphraseStrengthIsAGuide() {
        QCOMPARE(Vault::passphraseStrength("short"), 0);
        QCOMPARE(Vault::passphraseStrength("eightchr"), 1);
        QCOMPARE(Vault::passphraseStrength("twelve chars"), 2);
        QCOMPARE(Vault::passphraseStrength("correct horse battery"), 3);
    }

    // --- Entries ------------------------------------------------------------

    void entriesRoundTripExactly() {
        QTemporaryDir dir;
        const Crypto::SecretKey key = Crypto::SecretKey::random();
        Entry entry{EntryStore::newId(), EntryStore::now(),
                    QString::fromUtf8("# Morning\n\nNon breaking, émojis \U0001F600\n---\nnot front matter\n"),
                    {"tags: [kept]"}};
        QVERIFY(EntryStore::save(dir.path(), key, entry));

        QFile file(dir.filePath(entry.id + ".omj"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray onDisk = file.readAll();
        QVERIFY(!onDisk.contains("Morning"));
        QVERIFY(!onDisk.contains("created"));
        QCOMPARE(file.permissions() & (QFile::ReadGroup | QFile::ReadOther), QFileDevice::Permissions());

        const EntryStore::LoadResult loaded = EntryStore::loadAll(dir.path(), key);
        QVERIFY2(loaded.errors.isEmpty(), qPrintable(loaded.errors.join('\n')));
        QCOMPARE(loaded.entries.size(), 1);
        QCOMPARE(loaded.entries.first(), entry);
        QCOMPARE(loaded.entries.first().created.offsetFromUtc(), entry.created.offsetFromUtc());
    }

    void unreadableEntriesAreNamed() {
        QTemporaryDir dir;
        const Crypto::SecretKey key = Crypto::SecretKey::random();
        QVERIFY(EntryStore::save(dir.path(), key, {EntryStore::newId(), EntryStore::now(), "fine", {}}));
        QVERIFY(write(dir.filePath("broken.omj"), "OMJ1 but nothing else that makes sense here"));
        QVERIFY(EntryStore::save(dir.path(), Crypto::SecretKey::random(), {"otherkey", EntryStore::now(), "x", {}}));
        const EntryStore::LoadResult loaded = EntryStore::loadAll(dir.path(), key);
        QCOMPARE(loaded.entries.size(), 1);
        QCOMPARE(loaded.errors.size(), 2);
        QVERIFY(loaded.errors.join('\n').contains("broken.omj"));
        QVERIFY(loaded.errors.join('\n').contains("otherkey.omj"));
    }

    void timelineIsNewestFirst() {
        EntryModel model;
        const QDateTime base = EntryStore::now();
        model.setEntries({{"b", base.addDays(-1), "yesterday", {}}, {"c", base, "today", {}}});
        model.upsert({"a", base.addDays(-3), "older", {}});
        model.upsert({"d", base.addSecs(60), "newest", {}});
        QCOMPARE(model.idAt(0), QString("d"));
        QCOMPARE(model.idAt(1), QString("c"));
        QCOMPARE(model.idAt(2), QString("b"));
        QCOMPARE(model.idAt(3), QString("a"));
        model.upsert({"b", base.addDays(-1), "changed", {}});
        QCOMPARE(model.data(model.index(2), EntryModel::TitleRole).toString(), QString("changed"));
        QCOMPARE(model.data(model.index(2), EntryModel::DayRole).toString(),
                 base.addDays(-1).date().toString(Qt::ISODate));
        model.remove("c");
        QCOMPARE(model.count(), 3);
        QCOMPARE(model.indexOf("b"), 1);
    }

    void titlesAndExcerptsArePlain() {
        QCOMPARE(EntryModel::title("\n\n## **Big** day\nThen *this*"), QString("Big day"));
        QCOMPARE(EntryModel::excerpt("## Big day\n\n- one\n- two"), QString("one two"));
        QCOMPARE(EntryModel::title(""), QString());
        QCOMPARE(EntryModel::excerpt("only a title"), QString());
    }

    // --- Formatting and folding -------------------------------------------------

    void foldingIgnoresCaseAndAccents() {
        QCOMPARE(TextFold::fold(QString::fromUtf8("Café ÉCOLE ﬁn")), QString("cafe ecole fin"));
        QList<int> map;
        TextFold::fold(QString::fromUtf8("aé\U0001F600b"), &map);
        QCOMPARE(map, QList<int>({0, 1, 2, 2, 4}));
        QCOMPARE(TextFold::terms("  Page pages PAGE "), QStringList({"pages", "page"}));

        const QString text = QString::fromUtf8("Crème brûlée at the café, then crème again");
        const auto matches = TextFold::find(text, TextFold::terms("CREME cafe"));
        QCOMPARE(matches.size(), 3);
        QCOMPARE(text.mid(matches[0].start, matches[0].length), QString::fromUtf8("Crème"));
        QCOMPARE(text.mid(matches[1].start, matches[1].length), QString::fromUtf8("café"));
        QCOMPARE(text.mid(matches[2].start, matches[2].length), QString::fromUtf8("crème"));
        QVERIFY(TextFold::find(text, {}).isEmpty());
    }

    void markdownIsStyledWithoutChangingTheText() {
        QTextDocument document;
        const QString text = "# Big day\n"
                             "Some **bold**, *italic*, ~~gone~~ and `co*de*` here\n"
                             "- [x] done\n"
                             "> said\n"
                             "```\n"
                             "# not a heading\n"
                             "```\n"
                             "[site](https://example.com) and https://example.org";
        document.setPlainText(text);
        MarkdownHighlighter highlighter;
        highlighter.setBaseSize(16);
        highlighter.setMuted(QColor("#777777"));
        highlighter.setAccent(QColor("#ffd60a"));
        highlighter.setDocument(&document);
        // A new document is styled on the next pass of the event loop.
        highlighter.rehighlight();
        QCOMPARE(document.toPlainText(), text);

        // The heading is large and bold, its mark dimmed.
        QCOMPARE(formatAt(document, 0, 4).fontWeight(), int(QFont::Bold));
        QCOMPARE(formatAt(document, 0, 4).property(QTextFormat::FontPixelSize).toInt(), 26);
        QCOMPARE(formatAt(document, 0, 0).foreground().color(), QColor("#777777"));

        const QString line = text.section('\n', 1, 1);
        QCOMPARE(formatAt(document, 1, int(line.indexOf("bold"))).fontWeight(), int(QFont::Bold));
        QCOMPARE(formatAt(document, 1, int(line.indexOf("**"))).foreground().color(), QColor("#777777"));
        QVERIFY(formatAt(document, 1, int(line.indexOf("italic"))).fontItalic());
        QVERIFY(formatAt(document, 1, int(line.indexOf("gone"))).fontStrikeOut());
        QVERIFY(formatAt(document, 1, int(line.indexOf("co*"))).fontFixedPitch());
        // Inside code, a star is a star.
        QVERIFY(!formatAt(document, 1, int(line.indexOf("de*"))).fontItalic());
        QVERIFY(formatAt(document, 2, 7).fontStrikeOut());
        QVERIFY(formatAt(document, 3, 3).fontItalic());

        // A fence's lines are code, not Markdown.
        QVERIFY(formatAt(document, 5, 3).fontFixedPitch());
        QVERIFY(formatAt(document, 5, 3).fontWeight() != int(QFont::Bold));
        QCOMPARE(document.findBlockByNumber(5).userState(), int(MarkdownHighlighter::InFence));
        QCOMPARE(document.findBlockByNumber(7).userState(), int(MarkdownHighlighter::Normal));
        QVERIFY(formatAt(document, 7, 1).fontUnderline());
        QVERIFY(formatAt(document, 7, int(text.section('\n', 7, 7).indexOf("example.org"))).fontUnderline());

        // Search terms are marked wherever they are.
        highlighter.setMatchBackground(QColor("#00ff00"));
        highlighter.setSearch("BIG");
        QCOMPARE(formatAt(document, 0, 2).background().color(), QColor("#00ff00"));
        QCOMPARE(formatAt(document, 0, 2).fontWeight(), int(QFont::Bold));
        highlighter.setSearch("");
        QVERIFY(formatAt(document, 0, 2).background().style() == Qt::NoBrush);
    }

    // --- The backend ----------------------------------------------------------

    void backendSavesEncryptedAndForgetsOnLock() {
        QTemporaryDir dir;
        Vault vault(dir.filePath("settings.json"), dir.filePath("journal"), Crypto::KdfLimits::minimum());
        Backend backend(&vault);
        QSignalSpy created(&vault, &Vault::created);
        vault.create("correct horse battery");
        QTRY_COMPARE(created.size(), 1);
        QTRY_VERIFY(!backend.loading());

        backend.newEntry();
        QCOMPARE(backend.entries()->count(), 1);
        const QString first = backend.currentId();
        // An empty new entry is a fresh page already, and is never written.
        backend.newEntry();
        QCOMPARE(backend.currentId(), first);
        QVERIFY(QDir(vault.entriesPath()).isEmpty());

        backend.setText("Dear diary\nsecret plans");
        QCOMPARE(backend.saveState(), Backend::Unsaved);
        QTRY_COMPARE(backend.saveState(), Backend::Saved);
        QVERIFY(QFileInfo::exists(vault.entriesPath() + "/" + first + ".omj"));

        // Typing back to the saved text is clean again without a save.
        backend.setText("Dear diary\nsecret plans!");
        QCOMPARE(backend.saveState(), Backend::Unsaved);
        backend.setText("Dear diary\nsecret plans");
        QCOMPARE(backend.saveState(), Backend::Saved);

        // Leaving an empty new entry drops it.
        backend.newEntry();
        QCOMPARE(backend.entries()->count(), 2);
        QVERIFY(backend.openEntry(first));
        QCOMPARE(backend.entries()->count(), 1);
        QCOMPARE(backend.currentText(), QString("Dear diary\nsecret plans"));

        backend.setText("Dear diary\nsecret plans, revised");
        QVERIFY(backend.lock());
        QVERIFY(vault.locked());
        QCOMPARE(backend.entries()->count(), 0);
        QCOMPARE(backend.currentId(), QString());

        QSignalSpy unlocked(&vault, &Vault::unlocked);
        vault.unlock("correct horse battery");
        QTRY_COMPARE(unlocked.size(), 1);
        QTRY_COMPARE(backend.entries()->count(), 1);
        QCOMPARE(backend.currentId(), first);
        QCOMPARE(backend.currentText(), QString("Dear diary\nsecret plans, revised"));
    }

    void calendarKnowsWhichDaysHaveEntries() {
        QTemporaryDir dir;
        Vault vault(dir.filePath("settings.json"), dir.filePath("journal"), Crypto::KdfLimits::minimum());
        Backend backend(&vault);
        QSignalSpy created(&vault, &Vault::created), unlocked(&vault, &Vault::unlocked);
        vault.create("correct horse battery");
        QTRY_COMPARE(created.size(), 1);
        const QDateTime now = EntryStore::now();
        const QString today = now.date().toString(Qt::ISODate);
        const QString yesterday = now.addDays(-1).date().toString(Qt::ISODate);
        QVERIFY(EntryStore::save(vault.entriesPath(), *vault.key(), {"aa", now.addDays(-1).addSecs(-60), "early", {}}));
        QVERIFY(EntryStore::save(vault.entriesPath(), *vault.key(), {"bb", now.addDays(-1), "late", {}}));
        QVERIFY(EntryStore::save(vault.entriesPath(), *vault.key(), {"cc", now, "today", {}}));
        vault.lock();
        vault.unlock("correct horse battery");
        QTRY_COMPARE(unlocked.size(), 1);
        QTRY_COMPARE(backend.entries()->count(), 3);

        QSignalSpy daysChanged(&backend, &Backend::entryDaysChanged);
        QCOMPARE(backend.entryDays().value(yesterday).toInt(), 2);
        QCOMPARE(backend.entryDays().value(today).toInt(), 1);
        QCOMPARE(backend.currentDay(), today);

        // A day opens at its newest entry.
        QVERIFY(backend.openDay(yesterday));
        QCOMPARE(backend.currentId(), QString("bb"));
        QCOMPARE(backend.currentDay(), yesterday);
        QVERIFY(!backend.openDay(now.addDays(-30).date().toString(Qt::ISODate)));
        QCOMPARE(backend.currentId(), QString("bb"));

        // Typing changes no days; a new entry does.
        backend.setText("late, revised");
        QCOMPARE(daysChanged.size(), 0);
        backend.newEntry();
        QCOMPARE(backend.entryDays().value(today).toInt(), 2);
        QCOMPARE(daysChanged.size(), 1);
        QVERIFY(backend.lock());
    }

    void backendKeepsTheEntryWhenASaveFails() {
        QTemporaryDir dir;
        Vault vault(dir.filePath("settings.json"), dir.filePath("journal"), Crypto::KdfLimits::minimum());
        Backend backend(&vault);
        QSignalSpy created(&vault, &Vault::created);
        vault.create("correct horse battery");
        QTRY_COMPARE(created.size(), 1);
        QTRY_VERIFY(!backend.loading());

        backend.newEntry();
        QVERIFY(QFile::setPermissions(vault.entriesPath(), QFile::ReadOwner | QFile::ExeOwner));
        backend.setText("can't be written");
        QVERIFY(!backend.save());
        QCOMPARE(backend.saveState(), Backend::Failed);
        QVERIFY(!backend.lock());
        QVERIFY(!vault.locked());
        QCOMPARE(backend.currentText(), QString("can't be written"));
        QVERIFY(QFile::setPermissions(vault.entriesPath(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QVERIFY(backend.lock());
    }

    void themeFallsBack() {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QColor("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QColor("black"));
        QVERIFY(!theme.light());
        QCOMPARE(theme.radius(), 0);
    }

    void themeReadsThePalette() {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkpath("theme"));
        QVERIFY(write(dir.path() + "/theme/colors.toml", "mode = \"light\"\n"
                                                         "theme_type = \"dark\"\n"
                                                         "accent = \"#112233\"\n"
                                                         "bg = #fafafa # paper\n"
                                                         "foreground = '#202020'\n"
                                                         "color1 = \"#aa0000\"\n"));

        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QColor("#112233"));
        QCOMPARE(theme.accentForeground(), QColor("white"));
        QCOMPARE(theme.background(), QColor("#fafafa"));
        QCOMPARE(theme.foreground(), QColor("#202020"));
        QCOMPARE(theme.colors().value("red").value<QColor>(), QColor("#aa0000"));
        QVERIFY(theme.light());
    }

    void themeAsksHyprlandAgainOnlyWhenItReloads() {
        // The fake Hyprland lives outside the theme directory, whose changes
        // the Theme also follows.
        QTemporaryDir dir, hypr;
        const QString command = fakeHyprctl(hypr.path());
        QVERIFY(write(hypr.filePath("response.json"), "{\"int\":4}"));
        QLocalServer events;
        QVERIFY(events.listen(hypr.filePath("events.sock")));

        // Once on start, and once more when the event socket connects.
        Theme theme(dir.path(), nullptr, command, events.fullServerName());
        QTRY_VERIFY(events.hasPendingConnections());
        QLocalSocket *hyprland = events.nextPendingConnection();
        QTRY_COMPARE(lines(hypr.filePath("calls")), 2);
        QTRY_COMPARE(theme.radius(), 4);

        QVERIFY(write(hypr.filePath("response.json"), "{\"int\":10}"));
        QTest::qWait(300);
        QCOMPARE(lines(hypr.filePath("calls")), 2);
        // Events arrive in pieces; only a whole configreloaded line counts.
        hyprland->write("activewindow>>kitty,~\nconfigrel");
        hyprland->flush();
        QTest::qWait(100);
        QCOMPARE(lines(hypr.filePath("calls")), 2);
        hyprland->write("oaded>>\n");
        QTRY_COMPARE(theme.radius(), 10);
        QCOMPARE(lines(hypr.filePath("calls")), 3);
    }

    void themeReachesHyprlandOnceItListens() {
        QTemporaryDir dir, hypr;
        const QString command = fakeHyprctl(hypr.path());
        QVERIFY(write(hypr.filePath("response.json"), "{\"int\":4}"));
        Theme theme(dir.path(), nullptr, command, hypr.filePath("events.sock"));
        QTRY_COMPARE(theme.radius(), 4);

        QVERIFY(write(hypr.filePath("response.json"), "{\"int\":8}"));
        QLocalServer events;
        QVERIFY(events.listen(hypr.filePath("events.sock")));
        QTRY_VERIFY_WITH_TIMEOUT(events.hasPendingConnections(), 5000);
        QTRY_COMPARE(theme.radius(), 8);
    }

    void translatorFallsBackToEnglish() {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void translatorFollowsTheLanguageFile() {
        QTemporaryDir dir;
        QVERIFY(write(dir.path() + "/language", "pt-BR\n"));
        QVERIFY(write(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})"));
        QVERIFY(write(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})"));

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void translatorSeesAFirstPick() {
        // Before the first pick there is no ~/.config/omarchy to watch.
        QTemporaryDir dir;
        QVERIFY(write(dir.path() + "/da.json", R"({"Save": "Gem"})"));
        Translator translator(dir.path() + "/omarchy/language", dir.path());
        QVERIFY(QDir(dir.path()).mkpath("omarchy"));
        QVERIFY(write(dir.path() + "/omarchy/language", "da\n"));
        QTRY_COMPARE(translator.translate("Main", "Save"), QString("Gem"));
    }

    void translatorPicksPluralForms() {
        QTemporaryDir dir;
        QVERIFY(write(dir.path() + "/language", "ru\n"));
        QVERIFY(write(dir.path() + "/en.json", R"({"%n clips": {"one": "%n clip", "other": "%n clips"},
                                                   "%n files": {"one": "%n file", "other": "%n files"}})"));
        QVERIFY(write(dir.path() + "/ru.json", R"({"%n clips": {"one": "%n клип", "few": "%n клипа", "many": "%n клипов", "other": "%n клипа"}})"));

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 21), QString("%n клип"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 22), QString("%n клипа"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 25), QString("%n клипов"));
        // An untranslated counted string falls back to English, with English rules.
        QCOMPARE(translator.translate("Main", "%n files", nullptr, 21), QString("%n files"));
    }

    void englishCatalogListsEveryString() {
        // en.json is what translators work from: every text the sources
        // translate is in it, as itself or, when counted, as its English
        // plural forms, and nothing else is.
        const QString sources = QFileInfo(QFINDTESTDATA("../src/Main.qml")).absolutePath();
        QStringList problems;
        const QMap<QString, bool> used = translatables(sources, &problems);
        const QJsonObject english = readCatalog(sources + "/i18n/en.json");
        for (auto it = used.begin(); it != used.end(); ++it) {
            const QJsonValue value = english.value(it.key());
            if (value.isUndefined())
                problems << "missing from en.json: " + it.key();
            else if (it.value() && (value.toObject().keys() != QStringList{"one", "other"}
                                    || value.toObject().value("other").toString() != it.key()))
                problems << "counted, so needs its one and other forms: " + it.key();
            else if (!it.value() && value.toString() != it.key())
                problems << "should be itself: " + it.key();
        }
        for (const QString &key : english.keys())
            if (!used.contains(key))
                problems << "in en.json but no source translates it: " + key;
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join('\n')));
    }

    void catalogsMatchTheEnglishOne() {
        // A catalog may lack a string (it shows in English meanwhile), but what
        // it has translates something in en.json, with the same placeholders,
        // and a counted string has every form its language uses.
        const QString folder = QFileInfo(QFINDTESTDATA("../src/i18n/en.json")).absolutePath();
        const QJsonObject english = readCatalog(folder + "/en.json");
        QStringList problems;
        for (const QFileInfo &file : QDir(folder).entryInfoList({"*.json"}, QDir::Files)) {
            const QString code = file.completeBaseName();
            const QJsonObject catalog = readCatalog(file.filePath());
            if (catalog.isEmpty())
                problems << code + ": not a JSON object, or empty";
            // Some languages reach a form only at a million ("many" in French).
            QSet<QString> categories{"other", Translator::pluralCategory(code, 1000000)};
            for (long long n = 0; n <= 1000; ++n)
                categories << Translator::pluralCategory(code, n);
            for (auto it = catalog.begin(); it != catalog.end(); ++it) {
                const QString where = code + ": " + it.key() + ": ";
                const QJsonValue source = english.value(it.key());
                if (source.isUndefined()) {
                    problems << where + "not in en.json";
                } else if (source.isString()) {
                    if (!it.value().isString() || it.value().toString().isEmpty())
                        problems << where + "needs a translation";
                    else if (placeholders(it.value().toString()) != placeholders(it.key()))
                        problems << where + "has other placeholders than the English";
                } else {
                    const QJsonObject forms = it.value().toObject();
                    const QStringList wanted = placeholders(source.toObject().value("other").toString());
                    for (const QString &category : categories)
                        if (forms.value(category).toString().isEmpty())
                            problems << where + "needs its " + category + " form";
                    // A form other than "other" may say its number in words.
                    for (auto form = forms.begin(); form != forms.end(); ++form) {
                        const QStringList has = placeholders(form.value().toString());
                        QStringList needed = wanted;
                        if (form.key() != "other")
                            needed.removeIf([&](const QString &p) { return p.endsWith('n') && !has.contains(p); });
                        if (has != needed)
                            problems << where + form.key() + " has other placeholders than the English";
                    }
                }
            }
        }
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join('\n')));
    }

    void windowSetsUpWritesLocksAndUnlocks() {
        // A temp language, theme and journal, so the test never touches the desktop's.
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language");
        QCoreApplication::installTranslator(&translator);
        Theme theme(dir.path());
        Vault vault(dir.filePath("settings.json"), dir.filePath("journal"), Crypto::KdfLimits::minimum());
        Backend backend(&vault);

        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors)
                warnings << error.toString();
        });
        engine.rootContext()->setContextProperty("translator", &translator);
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("vault", &vault);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.load(QUrl("qrc:/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));

        auto item = [&](const char *name) { return window->findChild<QQuickItem *>(name); };
        auto click = [&](QQuickItem *target) {
            QVERIFY(target);
            // Items just shown are laid out on the next polish; until then
            // their position is stale.
            const auto centre = [&] { return target->mapToScene(QPointF(target->width() / 2, target->height() / 2)); };
            QVERIFY2(QTest::qWaitFor([&] {
                         return target->isVisible() && QRectF(0, 0, window->width(), window->height()).contains(centre());
                     }),
                     qPrintable(target->objectName() + " is never inside the window"));
            QTest::mouseClick(window, Qt::LeftButton, {}, centre().toPoint());
        };

        // Setup: the folder, the passphrase twice, then the recovery key.
        QTRY_VERIFY(item("setupWizard"));
        QCOMPARE(item("folderField")->property("text").toString(), vault.path());
        // The main action is filled with the accent, whatever Material.background says.
        auto *continueBackground = item("continueButton")->property("background").value<QQuickItem *>();
        QCOMPARE(continueBackground->property("color").value<QColor>(), theme.accent());
        click(item("continueButton"));
        QTRY_VERIFY(item("passphraseField")->hasActiveFocus());
        typeText(window, "correct horse battery");
        click(item("repeatField"));
        typeText(window, "correct horse battery");
        QTRY_VERIFY(item("createButton")->isEnabled());
        click(item("createButton"));
        QTRY_VERIFY(!item("recoveryKey")->property("text").toString().isEmpty());
        const QString recovery = item("recoveryKey")->property("text").toString();
        QVERIFY(!item("doneButton")->isEnabled());
        QTRY_VERIFY(item("confirmField")->hasActiveFocus());
        typeText(window, recovery.right(9));
        QTRY_VERIFY(item("doneButton")->isEnabled());
        click(item("doneButton"));
        QTRY_VERIFY(item("journalView"));

        // A new entry, typed into the editor, saved encrypted on its own.
        QTest::keyClick(window, Qt::Key_N, Qt::ControlModifier);
        QTRY_VERIFY(item("editor")->hasActiveFocus());
        typeText(window, "Morning pages");
        QTest::keyClick(window, Qt::Key_Return);
        typeText(window, "Woke early");
        QCOMPARE(backend.entries()->count(), 1);
        QCOMPARE(backend.entries()->data(backend.entries()->index(0), EntryModel::TitleRole).toString(),
                 QString("Morning pages"));
        QTRY_COMPARE(backend.saveState(), Backend::Saved);
        QCOMPARE(backend.currentText(), QString("Morning pages\nWoke early"));
        const QStringList files = QDir(vault.entriesPath()).entryList(QDir::Files);
        QCOMPARE(files.size(), 1);
        QFile file(vault.entriesPath() + "/" + files.first());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(!file.readAll().contains("Woke early"));

        // A non-breaking space comes through as itself.
        item("editor")->setProperty("text", QString::fromUtf8("Morning pages"));
        QCOMPARE(backend.currentText(), QString::fromUtf8("Morning pages"));
        QTRY_COMPARE(backend.saveState(), Backend::Saved);

        // The shortcuts overlay.
        auto *help = window->findChild<QObject *>("help");
        QTest::keyClick(window, Qt::Key_F1);
        QTRY_VERIFY(help->property("opened").toBool());
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!help->property("visible").toBool());

        // Locking forgets the entry; unlocking brings it back.
        QTest::keyClick(window, Qt::Key_L, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(item("unlockScreen"));
        QVERIFY(vault.locked());
        QCOMPARE(backend.entries()->count(), 0);
        QVERIFY(!item("journalView"));
        QTRY_VERIFY(item("unlockField")->hasActiveFocus());
        typeText(window, "wrong passphrase");
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(item("unlockError")->isVisible());
        QTRY_VERIFY(item("unlockField")->hasActiveFocus());
        typeText(window, "correct horse battery");
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(item("journalView"));
        QTRY_COMPARE(backend.entries()->count(), 1);
        QTRY_COMPARE(backend.currentText(), QString::fromUtf8("Morning\u00a0pages"));
        // The editor's text property shows a non-breaking space as a space, but
        // editing the reopened entry keeps it, since saving reads the document.
        item("editor")->forceActiveFocus();
        QTRY_VERIFY(item("editor")->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_End, Qt::ControlModifier);
        typeText(window, "!");
        QCOMPARE(backend.currentText(), QString::fromUtf8("Morning\u00a0pages!"));
        QTRY_COMPARE(backend.saveState(), Backend::Saved);

        // Formatting marks toggle around the selection.
        // A new entry starts beside existing ones, and stays open.
        QTest::keyClick(window, Qt::Key_N, Qt::ControlModifier);
        QCOMPARE(backend.entries()->count(), 2);
        QCOMPARE(backend.currentText(), QString());
        QTRY_VERIFY(item("editor")->hasActiveFocus());
        typeText(window, "word");
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(window, Qt::Key_B, Qt::ControlModifier);
        QCOMPARE(backend.currentText(), QString("**word**"));
        QTest::keyClick(window, Qt::Key_B, Qt::ControlModifier);
        QCOMPARE(backend.currentText(), QString("word"));
        QTest::keyClick(window, Qt::Key_I, Qt::ControlModifier);
        QCOMPARE(backend.currentText(), QString("*word*"));
        QTest::keyClick(window, Qt::Key_I, Qt::ControlModifier);
        QTest::keyClick(window, Qt::Key_End, Qt::ControlModifier);
        QTest::keyClick(window, Qt::Key_E, Qt::ControlModifier);
        typeText(window, "x");
        QCOMPARE(backend.currentText(), QString("word`x`"));
        QTRY_COMPARE(backend.saveState(), Backend::Saved);

        // The calendar takes the keyboard, and pages through months.
        QTest::keyClick(window, Qt::Key_1, Qt::ControlModifier);
        QTRY_VERIFY(item("calendar")->hasActiveFocus());
        const QString month = item("calendarMonth")->property("text").toString();
        QTest::keyClick(window, Qt::Key_PageDown);
        QTRY_VERIFY(item("calendarMonth")->property("text").toString() != month);
        QTest::keyClick(window, Qt::Key_Home);
        QTRY_COMPARE(item("calendarMonth")->property("text").toString(), month);
        QTest::keyClick(window, Qt::Key_Return);
        QCOMPARE(backend.currentDay(), QDate::currentDate().toString(Qt::ISODate));

        // Half a screen wide, the calendar folds into the timeline's pane.
        window->resize(800, 700);
        QTRY_VERIFY(!item("calendar")->isVisible());
        QTRY_VERIFY(item("narrowCalendar")->isVisible());
        QTest::keyClick(window, Qt::Key_1, Qt::ControlModifier);
        QTRY_VERIFY(item("narrowCalendar")->hasActiveFocus());
        window->resize(1100, 720);
        QTRY_VERIFY(item("calendar")->isVisible());

        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        QVERIFY(backend.lock());
        QCoreApplication::removeTranslator(&translator);
    }
};

QTEST_MAIN(OmajournalTests)
#include "omajournal_tests.moc"
