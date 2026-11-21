#include "crypto.h"

#include <QObject>
#include <cstring>
#include <sodium.h>

namespace Crypto {

namespace {

constexpr char fileMagic[] = "OMJ1";
constexpr int magicSize = 4;
constexpr int fileIdSize = 8;
constexpr int chunkSize = 64 * 1024;
constexpr int headerSize = magicSize + fileIdSize + crypto_secretstream_xchacha20poly1305_HEADERBYTES;
constexpr char fileContext[crypto_kdf_CONTEXTBYTES + 1] = "omj-file";
constexpr char recoveryContext[crypto_kdf_CONTEXTBYTES + 1] = "omj-recv";
constexpr char inboxContext[crypto_kdf_CONTEXTBYTES + 1] = "omj-inbx";
const char base32[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

static_assert(SecretKey::size == crypto_kdf_KEYBYTES);
static_assert(SecretKey::size == crypto_aead_xchacha20poly1305_ietf_KEYBYTES);
static_assert(SecretKey::size == crypto_secretstream_xchacha20poly1305_KEYBYTES);
static_assert(SecretKey::size == crypto_box_SECRETKEYBYTES);
static_assert(SecretKey::size == crypto_box_SEEDBYTES);
static_assert(SecretKey::size == crypto_sign_SEEDBYTES);

SecretKey fileKey(const SecretKey &masterKey, quint64 fileId) {
    SecretKey key;
    crypto_kdf_derive_from_key(key.data(), SecretKey::size, fileId, fileContext, masterKey.data());
    return key;
}

void fail(QString *error, const QString &message) {
    if (error)
        *error = message;
}

} // namespace

bool initialize() { return sodium_init() >= 0; }

SecretKey::SecretKey() : m_data(static_cast<unsigned char *>(sodium_malloc(size))) {
    // sodium_malloc already locks the page out of swap; a null key is one
    // the system refused to give locked memory for.
    if (m_data)
        sodium_memzero(m_data, size);
}

SecretKey::~SecretKey() {
    // sodium_free wipes before it frees.
    if (m_data)
        sodium_free(m_data);
}

SecretKey::SecretKey(SecretKey &&other) noexcept : m_data(other.m_data) { other.m_data = nullptr; }

SecretKey &SecretKey::operator=(SecretKey &&other) noexcept {
    if (this != &other) {
        if (m_data)
            sodium_free(m_data);
        m_data = other.m_data;
        other.m_data = nullptr;
    }
    return *this;
}

SecretKey SecretKey::random() {
    SecretKey key;
    if (!key.isNull())
        randombytes_buf(key.data(), size);
    return key;
}

SecretKey SecretKey::fromBytes(const QByteArray &bytes) {
    SecretKey key;
    if (!key.isNull() && bytes.size() == int(size))
        std::memcpy(key.data(), bytes.constData(), size);
    return key;
}

KdfLimits KdfLimits::moderate() {
    return {crypto_pwhash_OPSLIMIT_MODERATE, crypto_pwhash_MEMLIMIT_MODERATE};
}

KdfLimits KdfLimits::minimum() { return {crypto_pwhash_OPSLIMIT_MIN, crypto_pwhash_MEMLIMIT_MIN}; }

QByteArray randomBytes(int count) {
    QByteArray bytes(count, Qt::Uninitialized);
    randombytes_buf(bytes.data(), bytes.size());
    return bytes;
}

void wipe(QByteArray &bytes) {
    // data() detaches, so the wipe reaches this copy's own buffer.
    if (!bytes.isEmpty())
        sodium_memzero(bytes.data(), bytes.size());
    bytes.clear();
}

std::optional<SecretKey> keyFromPassphrase(const QByteArray &passphrase, const QByteArray &salt,
                                           const KdfLimits &limits) {
    if (salt.size() != crypto_pwhash_SALTBYTES)
        return std::nullopt;
    SecretKey key;
    if (key.isNull()
        || crypto_pwhash(key.data(), SecretKey::size, passphrase.constData(), passphrase.size(),
                         reinterpret_cast<const unsigned char *>(salt.constData()), limits.ops, limits.memory,
                         crypto_pwhash_ALG_ARGON2ID13)
               != 0)
        return std::nullopt;
    return key;
}

SecretKey keyFromRecovery(const QByteArray &recovery) {
    SecretKey source = SecretKey::fromBytes(recovery);
    SecretKey key;
    crypto_kdf_derive_from_key(key.data(), SecretKey::size, 1, recoveryContext, source.data());
    return key;
}

Wrapped wrap(const SecretKey &wrappingKey, const SecretKey &secret, const QByteArray &slot) {
    Wrapped wrapped;
    wrapped.nonce = randomBytes(crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);
    wrapped.ciphertext.resize(SecretKey::size + crypto_aead_xchacha20poly1305_ietf_ABYTES);
    unsigned long long length = 0;
    crypto_aead_xchacha20poly1305_ietf_encrypt(
        reinterpret_cast<unsigned char *>(wrapped.ciphertext.data()), &length, secret.data(), SecretKey::size,
        reinterpret_cast<const unsigned char *>(slot.constData()), slot.size(), nullptr,
        reinterpret_cast<const unsigned char *>(wrapped.nonce.constData()), wrappingKey.data());
    wrapped.ciphertext.resize(int(length));
    return wrapped;
}

std::optional<SecretKey> unwrap(const SecretKey &wrappingKey, const Wrapped &wrapped, const QByteArray &slot) {
    if (wrapped.nonce.size() != crypto_aead_xchacha20poly1305_ietf_NPUBBYTES
        || wrapped.ciphertext.size() != int(SecretKey::size + crypto_aead_xchacha20poly1305_ietf_ABYTES))
        return std::nullopt;
    SecretKey secret;
    unsigned long long length = 0;
    if (secret.isNull()
        || crypto_aead_xchacha20poly1305_ietf_decrypt(
               secret.data(), &length, nullptr, reinterpret_cast<const unsigned char *>(wrapped.ciphertext.constData()),
               wrapped.ciphertext.size(), reinterpret_cast<const unsigned char *>(slot.constData()), slot.size(),
               reinterpret_cast<const unsigned char *>(wrapped.nonce.constData()), wrappingKey.data())
               != 0
        || length != SecretKey::size)
        return std::nullopt;
    return secret;
}

QString encodeRecoveryKey(const QByteArray &bytes) {
    QString text;
    quint32 buffer = 0;
    int bits = 0;
    for (const char byte : bytes) {
        buffer = (buffer << 8) | quint8(byte);
        bits += 8;
        while (bits >= 5) {
            text += QChar(base32[(buffer >> (bits - 5)) & 31]);
            bits -= 5;
        }
    }
    if (bits > 0)
        text += QChar(base32[(buffer << (5 - bits)) & 31]);
    QString grouped;
    for (int i = 0; i < text.size(); i += 4)
        grouped += (i ? "-" : "") + text.mid(i, 4);
    return grouped;
}

QByteArray decodeRecoveryKey(const QString &text) {
    QByteArray bytes;
    quint32 buffer = 0;
    int bits = 0, characters = 0;
    for (QChar c : text.toUpper()) {
        if (c == '-' || c.isSpace())
            continue;
        // Base32 has no 0 or 1, so read them as the letters people mistake them for.
        if (c == '0')
            c = 'O';
        if (c == '1')
            c = 'I';
        const char *at = std::strchr(base32, c.toLatin1());
        if (!c.toLatin1() || !at)
            return {};
        buffer = (buffer << 5) | quint32(at - base32);
        bits += 5;
        ++characters;
        if (bits >= 8) {
            bytes += char((buffer >> (bits - 8)) & 0xff);
            bits -= 8;
        }
    }
    // 32 bytes are 52 characters, the last carrying four zero bits.
    if (characters != 52 || bytes.size() != int(SecretKey::size) || (buffer & ((1u << bits) - 1)) != 0)
        return {};
    return bytes;
}

namespace {

// A secretstream of 64 KiB chunks under key: its header, then the chunks,
// the last one tagged final. Shared by .omj and .oma files.
QByteArray pushStream(const SecretKey &key, const QByteArray &plaintext) {
    QByteArray stream(crypto_secretstream_xchacha20poly1305_HEADERBYTES, Qt::Uninitialized);
    crypto_secretstream_xchacha20poly1305_state state;
    crypto_secretstream_xchacha20poly1305_init_push(&state, reinterpret_cast<unsigned char *>(stream.data()),
                                                    key.data());
    // At least one chunk, so an empty entry still ends with a final tag.
    qsizetype offset = 0;
    do {
        const qsizetype length = std::min<qsizetype>(chunkSize, plaintext.size() - offset);
        const bool last = offset + length >= plaintext.size();
        const qsizetype at = stream.size();
        stream.resize(at + length + crypto_secretstream_xchacha20poly1305_ABYTES);
        crypto_secretstream_xchacha20poly1305_push(
            &state, reinterpret_cast<unsigned char *>(stream.data() + at), nullptr,
            reinterpret_cast<const unsigned char *>(plaintext.constData() + offset), length, nullptr, 0,
            last ? crypto_secretstream_xchacha20poly1305_TAG_FINAL : crypto_secretstream_xchacha20poly1305_TAG_MESSAGE);
        offset += length;
    } while (offset < plaintext.size());
    sodium_memzero(&state, sizeof state);
    return stream;
}

std::optional<QByteArray> pullStream(const SecretKey &key, const char *stream, qsizetype size, QString *error) {
    crypto_secretstream_xchacha20poly1305_state state;
    if (size < crypto_secretstream_xchacha20poly1305_HEADERBYTES
        || crypto_secretstream_xchacha20poly1305_init_pull(&state, reinterpret_cast<const unsigned char *>(stream),
                                                           key.data())
               != 0) {
        fail(error, QObject::tr("The file is damaged."));
        return std::nullopt;
    }
    QByteArray plaintext;
    qsizetype offset = crypto_secretstream_xchacha20poly1305_HEADERBYTES;
    bool finished = false;
    while (offset < size) {
        if (finished) {
            fail(error, QObject::tr("The file has data after its end, so it may have been tampered with."));
            return std::nullopt;
        }
        const qsizetype length = std::min<qsizetype>(chunkSize + crypto_secretstream_xchacha20poly1305_ABYTES,
                                                     size - offset);
        const qsizetype at = plaintext.size();
        plaintext.resize(at + length);
        unsigned long long written = 0;
        unsigned char tag = 0;
        if (crypto_secretstream_xchacha20poly1305_pull(
                &state, reinterpret_cast<unsigned char *>(plaintext.data() + at), &written, &tag,
                reinterpret_cast<const unsigned char *>(stream + offset), length, nullptr, 0)
            != 0) {
            wipe(plaintext);
            fail(error, QObject::tr("The file is damaged, or was encrypted with another key."));
            return std::nullopt;
        }
        plaintext.resize(at + qsizetype(written));
        finished = tag == crypto_secretstream_xchacha20poly1305_TAG_FINAL;
        offset += length;
    }
    sodium_memzero(&state, sizeof state);
    if (!finished) {
        wipe(plaintext);
        fail(error, QObject::tr("The file was cut short."));
        return std::nullopt;
    }
    return plaintext;
}

const unsigned char *bytes(const QByteArray &array) { return reinterpret_cast<const unsigned char *>(array.constData()); }

constexpr char agentMagic[] = "OMA1";
constexpr int sealSize = SecretKey::size + crypto_box_SEALBYTES;
constexpr int agentPrefixSize = magicSize + crypto_sign_PUBLICKEYBYTES + 2 * sealSize;

} // namespace

QByteArray encryptFile(const SecretKey &masterKey, const QByteArray &plaintext) {
    quint64 fileId = 0;
    randombytes_buf(&fileId, sizeof fileId);
    QByteArray file(magicSize + fileIdSize, Qt::Uninitialized);
    std::memcpy(file.data(), fileMagic, magicSize);
    for (int i = 0; i < fileIdSize; ++i)
        file[magicSize + i] = char((fileId >> (8 * i)) & 0xff);
    return file + pushStream(fileKey(masterKey, fileId), plaintext);
}

std::optional<QByteArray> decryptFile(const SecretKey &masterKey, const QByteArray &file, QString *error) {
    if (file.size() < headerSize || std::memcmp(file.constData(), fileMagic, magicSize) != 0) {
        fail(error, QObject::tr("This isn't an Omajournal file."));
        return std::nullopt;
    }
    quint64 fileId = 0;
    for (int i = 0; i < fileIdSize; ++i)
        fileId |= quint64(quint8(file[magicSize + i])) << (8 * i);
    const qsizetype at = magicSize + fileIdSize;
    return pullStream(fileKey(masterKey, fileId), file.constData() + at, file.size() - at, error);
}

BoxKeyPair inboxKeyPair(const SecretKey &masterKey) {
    SecretKey seed;
    crypto_kdf_derive_from_key(seed.data(), SecretKey::size, 1, inboxContext, masterKey.data());
    return boxKeyPairFromSeed(seed);
}

BoxKeyPair boxKeyPairFromSeed(const SecretKey &seed) {
    BoxKeyPair pair;
    pair.publicKey.resize(crypto_box_PUBLICKEYBYTES);
    crypto_box_seed_keypair(reinterpret_cast<unsigned char *>(pair.publicKey.data()), pair.secretKey.data(),
                            seed.data());
    return pair;
}

QByteArray signPublicKey(const SecretKey &signSeed) {
    QByteArray publicKey(crypto_sign_PUBLICKEYBYTES, Qt::Uninitialized);
    unsigned char secret[crypto_sign_SECRETKEYBYTES];
    crypto_sign_seed_keypair(reinterpret_cast<unsigned char *>(publicKey.data()), secret, signSeed.data());
    sodium_memzero(secret, sizeof secret);
    return publicKey;
}

QByteArray encryptAgentFile(const AgentKeys &agent, const QByteArray &journalPublicKey, const QByteArray &plaintext) {
    if (journalPublicKey.size() != crypto_box_PUBLICKEYBYTES)
        return {};
    const SecretKey key = SecretKey::random();
    const QByteArray boxPublic = boxKeyPairFromSeed(agent.boxSeed).publicKey;
    QByteArray file(agentMagic, magicSize);
    file += signPublicKey(agent.signSeed);
    for (const QByteArray &recipient : {journalPublicKey, boxPublic}) {
        QByteArray sealed(sealSize, Qt::Uninitialized);
        crypto_box_seal(reinterpret_cast<unsigned char *>(sealed.data()), key.data(), SecretKey::size,
                        bytes(recipient));
        file += sealed;
    }
    file += pushStream(key, plaintext);

    unsigned char publicKey[crypto_sign_PUBLICKEYBYTES], secret[crypto_sign_SECRETKEYBYTES];
    crypto_sign_seed_keypair(publicKey, secret, agent.signSeed.data());
    QByteArray signature(crypto_sign_BYTES, Qt::Uninitialized);
    crypto_sign_detached(reinterpret_cast<unsigned char *>(signature.data()), nullptr, bytes(file), file.size(),
                         secret);
    sodium_memzero(secret, sizeof secret);
    return file + signature;
}

std::optional<AgentFile> decryptAgentFile(const QByteArray &file, const BoxKeyPair &reader, bool asJournal,
                                          QString *error) {
    const qsizetype minimum = agentPrefixSize + crypto_secretstream_xchacha20poly1305_HEADERBYTES
                            + crypto_secretstream_xchacha20poly1305_ABYTES + crypto_sign_BYTES;
    if (file.size() < minimum || std::memcmp(file.constData(), agentMagic, magicSize) != 0) {
        fail(error, QObject::tr("This isn't an agent's entry file."));
        return std::nullopt;
    }
    // The signature first: nothing unsigned is decrypted.
    AgentFile result;
    result.signPublicKey = file.mid(magicSize, crypto_sign_PUBLICKEYBYTES);
    const qsizetype signedSize = file.size() - crypto_sign_BYTES;
    if (crypto_sign_verify_detached(bytes(file) + signedSize, bytes(file), signedSize, bytes(result.signPublicKey))
        != 0) {
        fail(error, QObject::tr("The file's signature doesn't match, so it may have been tampered with."));
        return std::nullopt;
    }
    const qsizetype sealAt = magicSize + crypto_sign_PUBLICKEYBYTES + (asJournal ? 0 : sealSize);
    SecretKey key;
    if (crypto_box_seal_open(key.data(), bytes(file) + sealAt, sealSize, bytes(reader.publicKey),
                             reader.secretKey.data())
        != 0) {
        fail(error, QObject::tr("The file wasn't written for this journal."));
        return std::nullopt;
    }
    auto plaintext = pullStream(key, file.constData() + agentPrefixSize, signedSize - agentPrefixSize, error);
    if (!plaintext)
        return std::nullopt;
    result.plaintext = std::move(*plaintext);
    return result;
}

} // namespace Crypto
