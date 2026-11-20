#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <memory>

#include "crypto.h"

// The journal on disk: its folder, the key file there, and while it is
// unlocked the master key in guarded memory. Deriving a key from a passphrase
// takes most of a second by design, so create, unlock and recover run on a
// worker thread and answer with a signal.
class Vault : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(QString pathProblem READ pathProblem NOTIFY pathChanged)
    Q_PROPERTY(bool exists READ exists NOTIFY stateChanged)
    Q_PROPERTY(bool locked READ locked NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    // Tests pass their own settings file, default folder, and cheap limits.
    explicit Vault(const QString &settingsFile = {}, const QString &defaultPath = {},
                   const Crypto::KdfLimits &limits = Crypto::KdfLimits::moderate(), QObject *parent = nullptr);
    ~Vault() override;

    QString path() const { return m_path; }
    void setPath(const QString &path);
    // Why a new journal can't go in path, or empty when it can.
    QString pathProblem() const;
    bool exists() const;
    bool locked() const { return !m_key; }
    bool busy() const { return m_watcher.isRunning(); }

    QString keyFile() const { return m_path + "/journal.key"; }
    QString entriesPath() const { return m_path + "/e"; }
    // Shared, so a worker reading entries keeps it alive past a lock.
    std::shared_ptr<const Crypto::SecretKey> key() const { return m_key; }

    // Starts a new journal in path; answers with created() or failed().
    Q_INVOKABLE void create(const QString &passphrase);
    // Answers with unlocked() or failed().
    Q_INVOKABLE void unlock(const QString &passphrase);
    // Unlocks with the recovery key and sets a new passphrase.
    Q_INVOKABLE void recover(const QString &recoveryKey, const QString &newPassphrase);
    // Checks the current passphrase and sets a new one; answers with
    // unlocked() or failed(), and leaves the journal unlocked.
    Q_INVOKABLE void changePassphrase(const QString &current, const QString &newPassphrase);
    Q_INVOKABLE void lock();

    // 0 too short, 1 weak, 2 fair, 3 strong. Only a guide.
    Q_INVOKABLE static int passphraseStrength(const QString &passphrase);
    // Whether text matches the last two groups of a recovery key.
    Q_INVOKABLE static bool confirmsRecoveryKey(const QString &recoveryKey, const QString &text);

signals:
    void pathChanged();
    void stateChanged();
    void busyChanged();
    void created(const QString &recoveryKey);
    void unlocked();
    void failed(const QString &message);

private:
    struct Result {
        std::shared_ptr<Crypto::SecretKey> key;
        QByteArray keyFile; // written by the GUI thread when not empty
        QString recoveryKey;
        QString error;
    };
    void run(std::function<Result()> work, bool created);
    bool writeSettings() const;

    QString m_settingsFile;
    QString m_path;
    Crypto::KdfLimits m_limits;
    std::shared_ptr<Crypto::SecretKey> m_key;
    QFutureWatcher<Result> m_watcher;
    bool m_creating = false;
};
