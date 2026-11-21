// omajournal — an encrypted journal for Omarchy.

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>
#include <cstdio>

#include "backend.h"
#include "cli.h"
#include "crypto.h"
#include "theme.h"
#include "translator.h"
#include "vault.h"

int main(int argc, char *argv[]) {
    // Only `omajournal open` shows a window; everything else, bare omajournal
    // included, is a command, and help for open needs no window either.
    // QGuiApplication aborts without a display before it reads its
    // arguments, so commands run offscreen even where the desktop exports
    // QT_QPA_PLATFORM=wayland.
    bool command = argc < 2 || qstrcmp(argv[1], "open") != 0;
    for (int i = 2; i < argc && !command; ++i)
        for (const char *option : {"-h", "--help", "-v", "--version"})
            command = command || qstrcmp(argv[i], option) == 0;
    if (command)
        qputenv("QT_QPA_PLATFORM", "offscreen");

    QGuiApplication app(argc, argv);
    app.setApplicationName("omajournal");
    app.setApplicationVersion("0.1.0");

    // Matches the window to omajournal.desktop, so the compositor (Wayland
    // app_id) and the launcher pick up the installed icon. The compiled-in
    // copy covers a run from build/, and needs qt6-svg's icon engine.
    app.setDesktopFileName("omajournal");
    app.setWindowIcon(QIcon::fromTheme("omajournal", QIcon(":/omajournal.svg")));

    // Without libsodium nothing can be read or written safely.
    if (!Crypto::initialize()) {
        std::fputs("omajournal: libsodium couldn't start\n", stderr);
        return 1;
    }

    if (command) {
        // The commands' own text is English, for scripts; the vault's
        // messages follow the Omarchy language, as in the window.
        Translator translator;
        app.installTranslator(&translator);
        return Cli::run(app.arguments());
    }

    QQuickStyle::setStyle("Material");
    Backend::registerQmlTypes();

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    Vault vault;
    Backend backend(&vault);

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("vault", &vault);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
