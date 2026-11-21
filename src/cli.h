#pragma once

#include <QString>
#include <QStringList>

// Omajournal's commands, which need no window: list, search, show, new,
// export, import and passwd. Each takes --json for output a script can read,
// exits 0 or 1, and writes errors to stderr. Commands that read or write the
// journal unlock it with the passphrase, asked for on the terminal with echo
// off or read from --passphrase-fd; never from an argument or the environment.
namespace Cli {

// Whether argv[1] names a command (or asks for help or the version), so main
// runs it without a window.
bool isCommand(const QString &word);
// Runs a command; arguments include the program name. Returns the exit code.
int run(const QStringList &arguments);
// The commands, one per line, for usage text.
QString summary();

} // namespace Cli
