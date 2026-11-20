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
const char base32[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

static_assert(SecretKey::size == crypto_kdf_KEYBYTES);
static_assert(SecretKey::size == crypto_aead_xchacha20poly1305_ietf_KEYBYTES);
static_assert(SecretKey::size == crypto_secretstream_xchacha20poly1305_KEYBYTES);

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

QByteArray encryptFile(const SecretKey &masterKey, const QByteArray &plaintext) {
    quint64 fileId = 0;
    randombytes_buf(&fileId, sizeof fileId);
    const SecretKey key = fileKey(masterKey, fileId);

    QByteArray file(headerSize, Qt::Uninitialized);
    std::memcpy(file.data(), fileMagic, magicSize);
    for (int i = 0; i < fileIdSize; ++i)
        file[magicSize + i] = char((fileId >> (8 * i)) & 0xff);
    crypto_secretstream_xchacha20poly1305_state state;
    crypto_secretstream_xchacha20poly1305_init_push(
        &state, reinterpret_cast<unsigned char *>(file.data() + magicSize + fileIdSize), key.data());

    // At least one chunk, so an empty entry still ends with a final tag.
    qsizetype offset = 0;
    do {
        const qsizetype length = std::min<qsizetype>(chunkSize, plaintext.size() - offset);
        const bool last = offset + length >= plaintext.size();
        const qsizetype at = file.size();
        file.resize(at + length + crypto_secretstream_xchacha20poly1305_ABYTES);
        crypto_secretstream_xchacha20poly1305_push(
            &state, reinterpret_cast<unsigned char *>(file.data() + at), nullptr,
            reinterpret_cast<const unsigned char *>(plaintext.constData() + offset), length, nullptr, 0,
            last ? crypto_secretstream_xchacha20poly1305_TAG_FINAL : crypto_secretstream_xchacha20poly1305_TAG_MESSAGE);
        offset += length;
    } while (offset < plaintext.size());
    sodium_memzero(&state, sizeof state);
    return file;
}

std::optional<QByteArray> decryptFile(const SecretKey &masterKey, const QByteArray &file, QString *error) {
    if (file.size() < headerSize || std::memcmp(file.constData(), fileMagic, magicSize) != 0) {
        fail(error, QObject::tr("This isn't an Omajournal file."));
        return std::nullopt;
    }
    quint64 fileId = 0;
    for (int i = 0; i < fileIdSize; ++i)
        fileId |= quint64(quint8(file[magicSize + i])) << (8 * i);
    const SecretKey key = fileKey(masterKey, fileId);

    crypto_secretstream_xchacha20poly1305_state state;
    if (crypto_secretstream_xchacha20poly1305_init_pull(
            &state, reinterpret_cast<const unsigned char *>(file.constData() + magicSize + fileIdSize), key.data())
        != 0) {
        fail(error, QObject::tr("The file is damaged."));
        return std::nullopt;
    }

    QByteArray plaintext;
    qsizetype offset = headerSize;
    bool finished = false;
    while (offset < file.size()) {
        if (finished) {
            fail(error, QObject::tr("The file has data after its end, so it may have been tampered with."));
            return std::nullopt;
        }
        const qsizetype length = std::min<qsizetype>(chunkSize + crypto_secretstream_xchacha20poly1305_ABYTES,
                                                     file.size() - offset);
        const qsizetype at = plaintext.size();
        plaintext.resize(at + length);
        unsigned long long written = 0;
        unsigned char tag = 0;
        if (crypto_secretstream_xchacha20poly1305_pull(
                &state, reinterpret_cast<unsigned char *>(plaintext.data() + at), &written, &tag,
                reinterpret_cast<const unsigned char *>(file.constData() + offset), length, nullptr, 0)
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

} // namespace Crypto
