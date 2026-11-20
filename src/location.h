#pragma once

#include <QDBusObjectPath>
#include <QObject>
#include <QTimer>
#include <QVariantMap>

// Where the computer is, asked for once. Tests stand in a fake.
class LocationSource : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    // Answers with located() or failed(), once.
    virtual void request() = 0;

signals:
    void located(double latitude, double longitude, double accuracy);
    void failed(const QString &message);
};

// The desktop's location, through xdg-desktop-portal's Location portal, which
// asks GeoClue and the user's permission.
class PortalLocation : public LocationSource {
    Q_OBJECT

public:
    explicit PortalLocation(QObject *parent = nullptr);
    ~PortalLocation() override;
    void request() override;

private slots:
    void onResponse(uint code, const QVariantMap &results);
    void onLocationUpdated(const QDBusObjectPath &session, const QVariantMap &location);

private:
    void finish();
    void fail(const QString &message);

    QString m_session;
    QString m_requestPath;
    QTimer m_timeout;
    bool m_busy = false;
};
