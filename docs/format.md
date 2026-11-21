# Omajournal's file format

This describes everything Omajournal writes, so a journal can always be read without the app.
All cryptography is [libsodium](https://doc.libsodium.org/).

## The folder

```
<journal>/
  journal.key     the master key, wrapped twice (JSON)
  e/<id>.omj      one encrypted file per entry
```

`<id>` is 32 random hex digits. File names carry no dates or titles. Files are created readable by
the owner only (0600), and the folder is 0700.

## `journal.key`

```json
{
  "format": "omajournal-key",
  "version": 1,
  "kdf": { "algorithm": "argon2id13", "opslimit": 3, "memlimit": 268435456, "salt": "<base64, 16 bytes>" },
  "passphrase": { "nonce": "<base64, 24 bytes>", "key": "<base64, 48 bytes>" },
  "recovery":   { "nonce": "<base64, 24 bytes>", "key": "<base64, 48 bytes>" }
}
```

A random 32-byte **master key** encrypts every entry. It's stored twice, each copy sealed with
`crypto_aead_xchacha20poly1305_ietf` under a different wrapping key:

- **passphrase**: the wrapping key is `crypto_pwhash` (Argon2id 1.3) of the passphrase, in Unicode NFC
  and UTF-8, with `kdf.salt`, `kdf.opslimit` and `kdf.memlimit`. The additional data is
  `omajournal key v1 passphrase`.
- **recovery**: the recovery key is 32 random bytes, shown once as base32 (RFC 4648, no padding) in
  13 groups of four. The wrapping key is `crypto_kdf_derive_from_key(id = 1, context = "omj-recv")`
  with those 32 bytes as the key. The additional data is `omajournal key v1 recovery`.

Changing the passphrase rewraps only the passphrase copy; entries don't change.

## Entry files (`.omj`)

| Bytes | Contents |
|---|---|
| 4 | `OMJ1` |
| 8 | file id, little-endian, random for every write |
| 24 | `crypto_secretstream_xchacha20poly1305` header |
| … | chunks |

The file key is `crypto_kdf_derive_from_key(id = file id, context = "omj-file")` with the master key.
The plaintext is split into chunks of 65 536 bytes, the last one shorter. Each chunk is pushed with
no additional data, so on disk each one is 17 bytes longer than its plaintext. The last chunk is
tagged `TAG_FINAL`, and the others `TAG_MESSAGE`. An empty entry is one empty final chunk. A file
without a final chunk was cut short, and one with data after it has been tampered with. Either way
it's rejected.

## Agent entry files (`.oma`)

Entries an agent writes go in `a/<id>.oma`. An agent never holds the master key: it seals each file
to the journal's **inbox key**, and to a key of its own so it can read back what it wrote, and signs
it.

- **The inbox keypair** is X25519 (`crypto_box_seed_keypair`), seeded with
  `crypto_kdf_derive_from_key(id = 1, context = "omj-inbx")` of the master key. Only its public half is
  stored, in `journal.pub`.
- **An agent's keys** are two 32-byte seeds: one for an X25519 keypair, one for an Ed25519 keypair
  (`crypto_sign_seed_keypair`).

| Bytes | Contents |
|---|---|
| 4 | `OMA1` |
| 32 | the agent's Ed25519 public key |
| 80 | `crypto_box_seal` of the file key, to the journal's inbox public key |
| 80 | `crypto_box_seal` of the file key, to the agent's X25519 public key |
| 24 | `crypto_secretstream_xchacha20poly1305` header |
| … | chunks, exactly as in `.omj` |
| 64 | `crypto_sign_detached` over every byte above, with the agent's Ed25519 key |

The file key is 32 random bytes, new for every write. The signature is checked before anything is
decrypted, and the signing key must belong to an agent the journal has paired.

## The plaintext of an entry

UTF-8: front matter between `---` lines, then the body in Markdown.

```
---
created: 2026-11-21T09:30:00+05:30
tags: [writing]
location: { lat: 51.507200, lon: -0.127600, accuracy: 40 }
---
# Morning pages

Woke early…
```

- `created` is ISO 8601 with the offset where the entry was written. Changing an entry's date or time
  keeps that offset.
- `tags`, if any: `tags: [work, ideas]`. Tags are lower case, without their `#`, and made of letters,
  digits, `_` and `-`. `#tags` written in the body also count as the entry's tags, but they aren't copied
  here.
- `location`, if any: `location: { lat: 9.931233, lon: 76.267304, accuracy: 120 }`. Degrees north and
  east, with negative numbers for south and west, and the accuracy in metres (0 when unknown).

Other front matter lines are kept as they are when an entry is saved, so a later version's fields
survive an older one. That includes a `location` line this version can't read.
