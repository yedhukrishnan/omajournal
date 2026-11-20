# Omajournal

![Omajournal's timeline and editor](docs/screenshot.png)

An encrypted journal for [Omarchy](https://omarchy.org). Write entries in Markdown, read them back in a
timeline, and keep them unreadable to anyone without your passphrase.

It follows the Omarchy theme, light or dark, Hyprland's window rounding, and the language picked under
Setup > Language, all live.

## Encryption

- Every entry is encrypted on its own with XChaCha20-Poly1305 (libsodium), under a master key that your
  passphrase unlocks through Argon2id. File names are random, so they give away no dates or titles.
- At setup you get a **recovery key**. It's the only way back in if you forget the passphrase. Keep it
  somewhere safe, such as a password manager or on paper.
- Locking (Ctrl+Shift+L, or closing the window) saves the open entry, then wipes the key and every
  decrypted entry from memory. Nothing decrypted is ever written to disk.
- **Not hidden:** how many entries there are, their sizes, and when files changed. The passphrase can't
  be reset without the recovery key: lose both and the journal is gone. Full-disk encryption is still
  worth having underneath.

The file format is in [docs/format.md](docs/format.md).

## Sync and backup

The journal is a plain folder (`~/Journal` unless you chose another) that holds only encrypted files,
so any tool can copy it without seeing what you wrote:

- **Sync between machines:** [Syncthing](https://syncthing.net) works well. Every entry is its own
  file, so two machines rarely touch the same one.
- **Backups:** [restic](https://restic.net) or borg keep snapshots, so a deleted or damaged entry can
  be brought back. Syncing alone copies a mistake everywhere.

## Keyboard

| Keys | Action |
|---|---|
| Ctrl+N | New entry |
| Ctrl+S | Save now (entries also save on their own as you write) |
| Ctrl+1 | Go to the timeline |
| Ctrl+2 | Go to the editor |
| ↑ ↓ | Move through the timeline |
| Enter | Edit the selected entry |
| Escape | From the editor back to the timeline |
| Ctrl+Z / Ctrl+Shift+Z | Undo / redo |
| Ctrl+Shift+L | Lock the journal |
| F1 | Show or hide the shortcuts |
| Ctrl+W / Ctrl+Q | Close or quit, locking first |

## Building

```sh
bin/build                                 # into build/
bin/test                                  # build, then run the tests offscreen
build/omajournal
cmake --install build --prefix ~/.local   # appear in the app launcher
```

It needs Qt 6 (base, declarative, svg, wayland), libsodium, and CMake.

## License

MIT. The build scripts, theme, and translator come from Omarchy's own apps.
