#include "location.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QRandomGenerator>

namespace {

const QString service = "org.freedesktop.portal.Desktop";
const QString desktopPath = "/org/freedesktop/portal/desktop";
const QString locationInterface = "org.freedesktop.portal.Location";

QString token() { return "omajournal" + QString::number(QRandomGenerator::global()->generate()); }

// The portal names requests after the caller's bus name: ":1.42" becomes "1_42".
QString senderPart(const QDBusConnection &bus) { return bus.baseService().mid(1).replace('.', '_'); }

} // namespace

PortalLocation::PortalLocation(QObject *parent) : LocationSource(parent) {
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(30000);
    connect(&m_timeout, &QTimer::timeout, this,
            [this] { fail(tr("No location came from the desktop. It may not be able to find one here.")); });
}

PortalLocation::~PortalLocation() { finish(); }

void PortalLocation::request() {
    if (m_busy)
        return;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        emit failed(tr("Location isn't available: there's no session bus."));
        return;
    }
    m_busy = true;
    m_timeout.start();

    bus.connect(service, desktopPath, locationInterface, "LocationUpdated", this,
                SLOT(onLocationUpdated(QDBusObjectPath, QVariantMap)));

    QDBusMessage create = QDBusMessage::createMethodCall(service, desktopPath, locationInterface, "CreateSession");
    // Accuracy 5 is "exact"; the portal and GeoClue give what they can.
    create << QVariantMap{{"session_handle_token", token()}, {"accuracy", uint(5)}};
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(create), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, bus](QDBusPendingCallWatcher *call) mutable {
        call->deleteLater();
        QDBusPendingReply<QDBusObjectPath> session = *call;
        if (!m_busy)
            return;
        if (session.isError()) {
            fail(tr("Location isn't available: %1").arg(session.error().message()));
            return;
        }
        m_session = session.value().path();

        // Listen for the answer before asking, so it can't arrive unheard.
        const QString handle = token();
        m_requestPath = desktopPath + "/request/" + senderPart(bus) + "/" + handle;
        bus.connect(service, m_requestPath, "org.freedesktop.portal.Request", "Response", this,
                    SLOT(onResponse(uint, QVariantMap)));
        QDBusMessage start = QDBusMessage::createMethodCall(service, desktopPath, locationInterface, "Start");
        start << QVariant::fromValue(QDBusObjectPath(m_session)) << QString() << QVariantMap{{"handle_token", handle}};
        auto *started = new QDBusPendingCallWatcher(bus.asyncCall(start), this);
        connect(started, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
            call->deleteLater();
            QDBusPendingReply<QDBusObjectPath> reply = *call;
            if (reply.isError() && m_busy)
                fail(tr("Location isn't available: %1").arg(reply.error().message()));
        });
    });
}

void PortalLocation::onResponse(uint code, const QVariantMap &) {
    if (!m_busy)
        return;
    // 0: started, and LocationUpdated follows. 1: the user said no. 2: it failed.
    if (code == 1)
        fail(tr("Location permission was refused. It can be allowed again in the desktop's privacy settings."));
    else if (code != 0)
        fail(tr("The desktop couldn't find your location. Location needs GeoClue (the geoclue package) installed and running."));
}

void PortalLocation::onLocationUpdated(const QDBusObjectPath &session, const QVariantMap &location) {
    if (!m_busy || session.path() != m_session)
        return;
    const double latitude = location.value("Latitude").toDouble();
    const double longitude = location.value("Longitude").toDouble();
    const double accuracy = location.value("Accuracy").toDouble();
    finish();
    emit located(latitude, longitude, accuracy);
}

void PortalLocation::fail(const QString &message) {
    finish();
    emit failed(message);
}

// One reading is all an entry needs, so the session closes after it.
void PortalLocation::finish() {
    if (!m_busy)
        return;
    m_busy = false;
    m_timeout.stop();
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.disconnect(service, desktopPath, locationInterface, "LocationUpdated", this,
                   SLOT(onLocationUpdated(QDBusObjectPath, QVariantMap)));
    if (!m_requestPath.isEmpty())
        bus.disconnect(service, m_requestPath, "org.freedesktop.portal.Request", "Response", this,
                       SLOT(onResponse(uint, QVariantMap)));
    if (!m_session.isEmpty())
        bus.asyncCall(QDBusMessage::createMethodCall(service, m_session, "org.freedesktop.portal.Session", "Close"));
    m_session.clear();
    m_requestPath.clear();
}
