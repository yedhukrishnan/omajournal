# Omajournal

![Omajournal's timeline and editor](docs/screenshot.png)

An encrypted journal for [Omarchy](https://omarchy.org). Write entries in Markdown, find them by day on
a calendar or in a timeline, and keep them unreadable to anyone without your passphrase.

- **Calendar:** a month at a time, with a dot on every day that has entries. Pick a day to read it.
- **Live formatting:** Markdown is styled as you type (headings, **bold**, *italic*, `code`, lists,
  quotes, links), with its marks dimmed. Ctrl+B, Ctrl+I and Ctrl+E toggle them. The file stays plain
  Markdown.
- **Tags:** add them with Ctrl+T, or write `#tags` in the text. The tag list beside the calendar narrows
  the timeline to one.
- **Where and when:** Ctrl+G records where you are, and Ctrl+D (or a click on the date) moves an entry to
  another day or time.
- **Writing font:** in Settings (Ctrl+,), pick the font entries are written in: the desktop's default,
  iA Writer Quattro, Duo or Mono, serif or monospace, with a size. Moving through the list previews each
  font in the editor. Only the editor changes; the entries stay plain Markdown.
- **Search:** Ctrl+F narrows the timeline to entries holding every word you type, whatever their case or
  accents, and highlights the matches.

It follows the Omarchy theme, light or dark, Hyprland's window rounding, and the language picked under
Setup > Language, all live.

## Encryption

- Every entry is encrypted on its own with XChaCha20-Poly1305 (libsodium), under a master key that your
  passphrase unlocks through Argon2id. File names are random, so they give away no dates or titles.
- At setup you get a **recovery key**. It's the only way back in if you forget the passphrase. Keep it
  somewhere safe, such as a password manager or on paper.
- Locking (Ctrl+Shift+L, or closing the window) saves the open entry, then wipes the key and every
  decrypted entry from memory. Nothing decrypted is ever written to disk.
- Omajournal turns off core dumps, so a crash can't write your entries to disk, and other programs
  running as you can't read its memory.
- **Not hidden:** how many entries there are, their sizes, and when files changed. The passphrase can't
  be reset without the recovery key: lose both and the journal is gone. Full-disk encryption is still
  worth having underneath.

The file format is in [docs/format.md](docs/format.md).

## Location

Ctrl+G asks the desktop where you are, through xdg-desktop-portal's Location portal, which uses
**GeoClue**. Install it with `omarchy-pkg-add geoclue` if Omajournal says location isn't available.
Depending on its setup, GeoClue may look nearby Wi-Fi networks up with an online location service. The
coordinates are stored only inside the encrypted entry. "Open in OpenStreetMap" sends them to
openstreetmap.org, and only when you choose it.

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
| Ctrl+F | Search entries (Down or Enter goes to the results, Escape closes) |
| Ctrl+B / Ctrl+I / Ctrl+E | Bold, italic, or code around the selection |
| Ctrl+T | Add a tag (Up/Down picks a suggestion) |
| Ctrl+G | Add where you are |
| Ctrl+D | Change the date and time |
| Ctrl+, | Settings (the writing font) |
| Ctrl++ / Ctrl+- / Ctrl+0 | Larger, smaller, or usual-size text |
| Ctrl+1 | Go to the calendar |
| Ctrl+2 | Go to the timeline |
| Ctrl+3 | Go to the editor |
| ← → ↑ ↓ | In the calendar: move through the days (Enter opens one, Home goes to today) |
| PgUp / PgDn | In the calendar: previous or next month |
| ↑ ↓ | In the timeline: move through the entries |
| Enter | Edit the selected entry |
| Escape | From the editor back to the timeline |
| Ctrl+Z / Ctrl+Shift+Z | Undo / redo |
| Ctrl+Shift+L | Lock the journal |
| F1 | Show or hide the shortcuts |
| Ctrl+W / Ctrl+Q | Close or quit, locking first |

## Command line

`omajournal open` starts the window. Everything else is a command that needs no display, for scripts
and backups:

| Command | Does |
|---|---|
| `omajournal new [--text TEXT] [--tag T]... [--date YYYY-MM-DD] [--time HH:MM]` | Write an entry, from `--text` or stdin, and print its id |
| `omajournal list [--month YYYY-MM] [--tag T]` | List entries, newest first |
| `omajournal search WORDS... [--tag T]` | Find entries holding every word, with a snippet |
| `omajournal show ID` | Print an entry; the start of its id is enough |
| `omajournal export --decrypt DIR` | Write every entry as a Markdown file into a new or empty folder |
| `omajournal import DIR` | Add every `.md` file in a folder as a new entry |
| `omajournal passwd` | Change the passphrase |

Each command asks for the passphrase on the terminal, with echo off. A script can give it on a file
descriptor instead with `--passphrase-fd N` (for `passwd`, the current passphrase on the first line and
the new one on the second); it is never read from an argument or the environment, where other programs
could see it. `--journal DIR` works on another journal, and `--json` prints a JSON object. Errors go
to stderr, and a failed command exits with 1.

```sh
omajournal new --tag work --text "Shipped the release."
pass show journal | omajournal list --month 2026-11 --json --passphrase-fd 0
```

**`export --decrypt` writes your entries unencrypted.** Anyone who can read that folder can read them,
so delete it when you're done. Exported files keep their front matter, so `import` brings back their
dates and tags; a file without front matter is dated by when it last changed.

## Building

```sh
bin/build                                 # into build/
bin/test                                  # build, then run the tests offscreen
build/omajournal open
cmake --install build --prefix ~/.local   # appear in the app launcher
```

It needs Qt 6 (base, declarative, svg, wayland), libsodium, and CMake.

## License

MIT. The build scripts, theme, and translator come from Omarchy's own apps.
