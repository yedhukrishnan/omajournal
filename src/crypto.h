#pragma once

#include <QByteArray>
#include <QString>
#include <cstddef>
#include <optional>

// The journal's cryptography, on libsodium. Nothing here touches the disk or
// the GUI, so every piece is tested on its own. docs/format.md describes the
// formats these functions read and write.
namespace Crypto {

// Call once before anything else; false if libsodium can't start.
bool initialize();

// A 32-byte key in memory libsodium guards: locked out of swap, fenced by
// guard pages, and wiped when it goes.
class SecretKey {
public:
    static constexpr std::size_t size = 32;

    SecretKey();
    ~SecretKey();
    SecretKey(SecretKey &&other) noexcept;
    SecretKey &operator=(SecretKey &&other) noexcept;
    SecretKey(const SecretKey &) = delete;
    SecretKey &operator=(const SecretKey &) = delete;

    static SecretKey random();
    // Takes size bytes; the caller wipes its own copy.
    static SecretKey fromBytes(const QByteArray &bytes);

    bool isNull() const { return m_data == nullptr; }
    unsigned char *data() { return m_data; }
    const unsigned char *data() const { return m_data; }

private:
    unsigned char *m_data = nullptr;
};

// How hard Argon2id works on a passphrase. The journal uses moderate; the
// tests use the minimum so they run in milliseconds.
struct KdfLimits {
    unsigned long long ops;
    std::size_t memory;
    static KdfLimits moderate();
    static KdfLimits minimum();
};

QByteArray randomBytes(int count);
void wipe(QByteArray &bytes);

// The key that wraps the master key, from a passphrase and its salt.
std::optional<SecretKey> keyFromPassphrase(const QByteArray &passphrase, const QByteArray &salt,
                                           const KdfLimits &limits);
// The key that wraps the master key, from the 32 bytes of a recovery key.
SecretKey keyFromRecovery(const QByteArray &recovery);

// The master key sealed under a wrapping key, bound to the slot's name.
struct Wrapped {
    QByteArray nonce;
    QByteArray ciphertext;
};
Wrapped wrap(const SecretKey &wrappingKey, const SecretKey &secret, const QByteArray &slot);
std::optional<SecretKey> unwrap(const SecretKey &wrappingKey, const Wrapped &wrapped, const QByteArray &slot);

// A recovery key as people read and type it: base32 in groups of four.
QString encodeRecoveryKey(const QByteArray &bytes);
// Empty when the text isn't a recovery key. Case, spaces and dashes don't matter.
QByteArray decodeRecoveryKey(const QString &text);

// A whole file: OMJ1, its id, and a secretstream of 64 KiB chunks under a key
// derived from the master key and that id.
QByteArray encryptFile(const SecretKey &masterKey, const QByteArray &plaintext);
std::optional<QByteArray> decryptFile(const SecretKey &masterKey, const QByteArray &file, QString *error = nullptr);

// --- Entries written by agents (docs/format.md, "Agent entry files") -------

// An X25519 keypair. Anyone with the public half can seal to it.
struct BoxKeyPair {
    QByteArray publicKey;
    SecretKey secretKey;
};

// The journal's inbox keypair, derived from the master key whenever it's
// needed, so only the public half (journal.pub) is ever stored.
BoxKeyPair inboxKeyPair(const SecretKey &masterKey);
BoxKeyPair boxKeyPairFromSeed(const SecretKey &seed);
// The Ed25519 public key for a signing seed.
QByteArray signPublicKey(const SecretKey &signSeed);

// What an agent holds: a seed for the X25519 keypair it reads its own entries
// back with, and one for the Ed25519 keypair it signs them with.
struct AgentKeys {
    SecretKey boxSeed;
    SecretKey signSeed;
};

// An OMA1 file: the agent's signing key, a fresh file key sealed to the
// journal and to the agent, the stream, and a signature over all of it.
// Empty when the journal's public key isn't one.
QByteArray encryptAgentFile(const AgentKeys &agent, const QByteArray &journalPublicKey, const QByteArray &plaintext);

struct AgentFile {
    QByteArray signPublicKey; // who signed it; the caller checks it is paired
    QByteArray plaintext;
};
// Checks the signature, then opens the file key's copy for the journal
// (asJournal, with the inbox keypair) or for the agent (its own keypair).
std::optional<AgentFile> decryptAgentFile(const QByteArray &file, const BoxKeyPair &reader, bool asJournal,
                                          QString *error = nullptr);

} // namespace Crypto
