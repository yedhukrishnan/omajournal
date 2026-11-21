#include "vault.h"

#include "settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QtConcurrent>
#include <sodium.h>

namespace {

const QByteArray passphraseSlot = "omajournal key v1 passphrase";
const QByteArray recoverySlot = "omajournal key v1 recovery";

// The same passphrase typed through different input methods can arrive
// composed or decomposed; both unlock.
QByteArray passphraseBytes(const QString &passphrase) {
    return passphrase.normalized(QString::NormalizationForm_C).toUtf8();
}

QString base64(const QByteArray &bytes) { return QString::fromLatin1(bytes.toBase64()); }
QByteArray unbase64(const QJsonValue &value) { return QByteArray::fromBase64(value.toString().toLatin1()); }

QJsonObject wrappedJson(const Crypto::Wrapped &wrapped) {
    return {{"nonce", base64(wrapped.nonce)}, {"key", base64(wrapped.ciphertext)}};
}

Crypto::Wrapped wrappedFrom(const QJsonValue &value) {
    return {unbase64(value["nonce"]), unbase64(value["key"])};
}

QJsonObject kdfJson(const QByteArray &salt, const Crypto::KdfLimits &limits) {
    return {{"algorithm", "argon2id13"},
            {"opslimit", double(limits.ops)},
            {"memlimit", double(limits.memory)},
            {"salt", base64(salt)}};
}

QString expandHome(const QString &path) {
    if (path == "~")
        return QDir::homePath();
    if (path.startsWith("~/"))
        return QDir::homePath() + path.mid(1);
    return path;
}

QString normalizedRecovery(const QString &text) {
    QString result;
    for (const QChar c : text)
        if (c != '-' && !c.isSpace())
            result += c.toUpper();
    return result;
}

} // namespace

Vault::Vault(const QString &settingsFile, const QString &defaultPath, const Crypto::KdfLimits &limits, QObject *parent)
    : QObject(parent),
      m_settingsFile(settingsFile.isEmpty() ? Settings::defaultFile() : settingsFile), m_limits(limits) {
    m_path = Settings::read(m_settingsFile).value("journal").toString();
    if (m_path.isEmpty())
        m_path = defaultPath.isEmpty() ? QDir::homePath() + "/Journal" : defaultPath;

    connect(&m_watcher, &QFutureWatcher<Result>::started, this, &Vault::busyChanged);
    connect(&m_watcher, &QFutureWatcher<Result>::finished, this, [this] {
        const bool creating = m_creating;
        m_creating = false;
        Result result = m_watcher.result();
        emit busyChanged();
        if (!result.error.isEmpty()) {
            emit failed(result.error);
            return;
        }
        if (!result.keyFile.isEmpty()) {
            if (creating) {
                QDir().mkpath(m_path);
                QFile::setPermissions(m_path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
                QDir().mkpath(entriesPath());
            }
            // A key file that can't be written fully is never left half
            // written: without it the journal can't be opened again.
            QSaveFile file(keyFile());
            file.setDirectWriteFallback(false);
            if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)
                || file.write(result.keyFile) != result.keyFile.size() || !file.commit()) {
                emit failed(tr("Couldn't save the key file: %1").arg(file.errorString()));
                return;
            }
        }
        m_key = result.key;
        // Without the setting, the next start looks in the default folder.
        if (creating && !writeSettings())
            emit failed(tr("The journal was made, but its folder couldn't be saved in %1. Choose %2 again next time.")
                            .arg(m_settingsFile, m_path));
        emit stateChanged();
        if (creating)
            emit created(result.recoveryKey);
        else
            emit unlocked();
    });
}

Vault::~Vault() { m_watcher.waitForFinished(); }

void Vault::setPath(const QString &path) {
    const QString expanded = QDir::cleanPath(expandHome(path.trimmed()));
    if (!locked() || busy() || expanded == m_path)
        return;
    m_path = expanded;
    emit pathChanged();
    emit stateChanged();
}

QString Vault::displayPath() const {
    const QString home = QDir::homePath();
    if (m_path == home)
        return "~";
    if (m_path.startsWith(home + "/"))
        return "~" + m_path.mid(home.size());
    return m_path;
}

QString Vault::pathProblem() const {
    if (m_path.isEmpty() || m_path == ".")
        return tr("Choose a folder for the journal.");
    if (!QDir::isAbsolutePath(m_path))
        return tr("Use a full path, such as ~/Journal.");
    const QFileInfo info(m_path);
    if (info.exists() && !info.isDir())
        return tr("That's a file, not a folder.");
    if (exists())
        return {};
    if (info.exists() && !QDir(m_path).isEmpty(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot))
        return tr("This folder already has other files in it. Choose an empty folder, or one that holds a journal.");
    return {};
}

bool Vault::exists() const { return QFileInfo::exists(keyFile()); }

void Vault::create(const QString &passphrase) {
    if (busy() || exists() || !pathProblem().isEmpty())
        return;
    QByteArray secret = passphraseBytes(passphrase);
    const Crypto::KdfLimits limits = m_limits;
    run([secret, limits]() mutable {
        Result result;
        auto masterKey = std::make_shared<Crypto::SecretKey>(Crypto::SecretKey::random());
        QByteArray recovery = Crypto::randomBytes(Crypto::SecretKey::size);
        const QByteArray salt = Crypto::randomBytes(crypto_pwhash_SALTBYTES);
        auto passphraseKey = Crypto::keyFromPassphrase(secret, salt, limits);
        Crypto::wipe(secret);
        if (masterKey->isNull() || !passphraseKey) {
            Crypto::wipe(recovery);
            result.error = tr("Couldn't make the keys: the system is out of memory.");
            return result;
        }
        const QJsonObject file{
            {"format", "omajournal-key"},
            {"version", 1},
            {"kdf", kdfJson(salt, limits)},
            {"passphrase", wrappedJson(Crypto::wrap(*passphraseKey, *masterKey, passphraseSlot))},
            {"recovery", wrappedJson(Crypto::wrap(Crypto::keyFromRecovery(recovery), *masterKey, recoverySlot))},
        };
        result.keyFile = QJsonDocument(file).toJson();
        result.recoveryKey = Crypto::encodeRecoveryKey(recovery);
        Crypto::wipe(recovery);
        result.key = masterKey;
        return result;
    }, true);
}

void Vault::unlock(const QString &passphrase) {
    if (busy() || !locked())
        return;
    QFile file(keyFile());
    if (!file.open(QIODevice::ReadOnly)) {
        emit failed(tr("Couldn't read the key file: %1").arg(file.errorString()));
        return;
    }
    const QJsonObject keys = QJsonDocument::fromJson(file.readAll()).object();
    QByteArray secret = passphraseBytes(passphrase);
    run([keys, secret]() mutable {
        Result result;
        const QJsonObject kdf = keys["kdf"].toObject();
        if (keys["format"] != "omajournal-key" || keys["version"].toInt() != 1
            || kdf["algorithm"] != "argon2id13") {
            Crypto::wipe(secret);
            result.error = tr("The key file isn't one this version of Omajournal can read.");
            return result;
        }
        const Crypto::KdfLimits limits{static_cast<unsigned long long>(kdf["opslimit"].toDouble()),
                                       static_cast<std::size_t>(kdf["memlimit"].toDouble())};
        auto passphraseKey = Crypto::keyFromPassphrase(secret, unbase64(kdf["salt"]), limits);
        Crypto::wipe(secret);
        std::optional<Crypto::SecretKey> masterKey;
        if (passphraseKey)
            masterKey = Crypto::unwrap(*passphraseKey, wrappedFrom(keys["passphrase"]), passphraseSlot);
        if (!masterKey) {
            result.error = tr("That passphrase doesn't open this journal.");
            return result;
        }
        result.key = std::make_shared<Crypto::SecretKey>(std::move(*masterKey));
        return result;
    }, false);
}

void Vault::recover(const QString &recoveryKey, const QString &newPassphrase) {
    if (busy() || !locked())
        return;
    QFile file(keyFile());
    if (!file.open(QIODevice::ReadOnly)) {
        emit failed(tr("Couldn't read the key file: %1").arg(file.errorString()));
        return;
    }
    QJsonObject keys = QJsonDocument::fromJson(file.readAll()).object();
    QByteArray recovery = Crypto::decodeRecoveryKey(recoveryKey);
    if (recovery.isEmpty()) {
        emit failed(tr("That isn't a recovery key. It has 13 groups of four letters and digits."));
        return;
    }
    QByteArray secret = passphraseBytes(newPassphrase);
    const Crypto::KdfLimits limits = m_limits;
    run([keys, recovery, secret, limits]() mutable {
        Result result;
        auto masterKey = Crypto::unwrap(Crypto::keyFromRecovery(recovery), wrappedFrom(keys["recovery"]), recoverySlot);
        Crypto::wipe(recovery);
        if (!masterKey) {
            Crypto::wipe(secret);
            result.error = tr("That recovery key doesn't open this journal.");
            return result;
        }
        // Only the passphrase's copy of the master key changes; every entry
        // stays as it is.
        const QByteArray salt = Crypto::randomBytes(crypto_pwhash_SALTBYTES);
        auto passphraseKey = Crypto::keyFromPassphrase(secret, salt, limits);
        Crypto::wipe(secret);
        if (!passphraseKey) {
            result.error = tr("Couldn't make the keys: the system is out of memory.");
            return result;
        }
        keys["kdf"] = kdfJson(salt, limits);
        keys["passphrase"] = wrappedJson(Crypto::wrap(*passphraseKey, *masterKey, passphraseSlot));
        result.keyFile = QJsonDocument(keys).toJson();
        result.key = std::make_shared<Crypto::SecretKey>(std::move(*masterKey));
        return result;
    }, false);
}

void Vault::changePassphrase(const QString &current, const QString &newPassphrase) {
    if (busy() || !locked())
        return;
    QFile file(keyFile());
    if (!file.open(QIODevice::ReadOnly)) {
        emit failed(tr("Couldn't read the key file: %1").arg(file.errorString()));
        return;
    }
    QJsonObject keys = QJsonDocument::fromJson(file.readAll()).object();
    QByteArray old = passphraseBytes(current);
    QByteArray secret = passphraseBytes(newPassphrase);
    const Crypto::KdfLimits limits = m_limits;
    run([keys, old, secret, limits]() mutable {
        Result result;
        const QJsonObject kdf = keys["kdf"].toObject();
        const Crypto::KdfLimits oldLimits{static_cast<unsigned long long>(kdf["opslimit"].toDouble()),
                                          static_cast<std::size_t>(kdf["memlimit"].toDouble())};
        auto oldKey = Crypto::keyFromPassphrase(old, unbase64(kdf["salt"]), oldLimits);
        Crypto::wipe(old);
        std::optional<Crypto::SecretKey> masterKey;
        if (oldKey)
            masterKey = Crypto::unwrap(*oldKey, wrappedFrom(keys["passphrase"]), passphraseSlot);
        if (!masterKey) {
            Crypto::wipe(secret);
            result.error = tr("That passphrase doesn't open this journal.");
            return result;
        }
        // As with recovery, only the passphrase's copy of the master key changes.
        const QByteArray salt = Crypto::randomBytes(crypto_pwhash_SALTBYTES);
        auto passphraseKey = Crypto::keyFromPassphrase(secret, salt, limits);
        Crypto::wipe(secret);
        if (!passphraseKey) {
            result.error = tr("Couldn't make the keys: the system is out of memory.");
            return result;
        }
        keys["kdf"] = kdfJson(salt, limits);
        keys["passphrase"] = wrappedJson(Crypto::wrap(*passphraseKey, *masterKey, passphraseSlot));
        result.keyFile = QJsonDocument(keys).toJson();
        result.key = std::make_shared<Crypto::SecretKey>(std::move(*masterKey));
        return result;
    }, false);
}

void Vault::lock() {
    if (locked())
        return;
    // The key is wiped once no worker still holds it.
    m_key.reset();
    emit stateChanged();
}

void Vault::run(std::function<Result()> work, bool creating) {
    m_creating = creating;
    m_watcher.setFuture(QtConcurrent::run(std::move(work)));
}

bool Vault::writeSettings() const {
    // Only the journal's key changes, so the preferences stay as they are.
    return Settings::update(m_settingsFile, {{"journal", m_path}});
}

int Vault::passphraseStrength(const QString &passphrase) {
    const int length = passphrase.size();
    if (length < 8)
        return 0;
    bool lower = false, upper = false, digit = false, other = false;
    for (const QChar c : passphrase) {
        lower |= c.isLower();
        upper |= c.isUpper();
        digit |= c.isDigit();
        other |= !c.isLetterOrNumber();
    }
    const int kinds = lower + upper + digit + other;
    if (length >= 16 || (length >= 12 && kinds >= 3))
        return 3;
    if (length >= 12 || kinds >= 3)
        return 2;
    return 1;
}

bool Vault::confirmsRecoveryKey(const QString &recoveryKey, const QString &text) {
    const QString key = normalizedRecovery(recoveryKey), typed = normalizedRecovery(text);
    return key.size() > 8 && typed == key.right(8);
}
