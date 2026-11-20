// omajournal — an encrypted journal for Omarchy.

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>
#include <cstdio>

#include "backend.h"
#include "crypto.h"
#include "theme.h"
#include "translator.h"
#include "vault.h"

int main(int argc, char *argv[]) {
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
