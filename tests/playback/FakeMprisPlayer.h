#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>

#include "playback/MprisPlayer.h"

// Minimal org.mpris.MediaPlayer2.Player served from its own bus connection.
class FakePlayerAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QVariantMap Metadata MEMBER metadata)
    Q_PROPERTY(QString PlaybackStatus MEMBER status)
    Q_PROPERTY(qlonglong Position MEMBER positionUs)
    Q_PROPERTY(double Rate MEMBER rate)
public:
    explicit FakePlayerAdaptor(QObject *parent) : QDBusAbstractAdaptor(parent) {}
    QVariantMap metadata;
    QString status = QStringLiteral("Stopped");
    qlonglong positionUs = 0;
    double rate = 1.0;
signals:
    void Seeked(qlonglong position);
};

class FakeMprisPlayer {
public:
    explicit FakeMprisPlayer(const QString &name)
        : m_conn(QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("fake-") + name)),
          m_service(Mpris::kServicePrefix + name)
    {
        m_adaptor = new FakePlayerAdaptor(&m_root);
        m_conn.registerObject(Mpris::kPath, &m_root);
        m_conn.registerService(m_service);
    }
    ~FakeMprisPlayer()
    {
        m_conn.unregisterService(m_service);
        QDBusConnection::disconnectFromBus(m_conn.name());
    }

    QString service() const { return m_service; }

    void setTrack(const QString &id, const QString &title, const QStringList &artists, qint64 lengthMs,
                  const QString &art = {})
    {
        QVariantMap md{{QStringLiteral("mpris:trackid"), QVariant::fromValue(QDBusObjectPath(QStringLiteral("/track/") + id))},
                       {QStringLiteral("xesam:title"), title}};
        if (!artists.isEmpty())
            md.insert(QStringLiteral("xesam:artist"), artists);
        if (lengthMs > 0)
            md.insert(QStringLiteral("mpris:length"), qlonglong(lengthMs * 1000));
        if (!art.isEmpty())
            md.insert(QStringLiteral("mpris:artUrl"), art);
        m_adaptor->metadata = md;
        notify({{QStringLiteral("Metadata"), md}});
    }
    void setStatus(const QString &status, qint64 positionMs)
    {
        m_adaptor->status = status;
        m_adaptor->positionUs = positionMs * 1000;
        notify({{QStringLiteral("PlaybackStatus"), status}}); // Position is never signaled, per MPRIS
    }
    // Moves the playhead without any signal, as a buffering stall does.
    void setPositionSilently(qint64 positionMs) { m_adaptor->positionUs = positionMs * 1000; }
    void seek(qint64 positionMs)
    {
        m_adaptor->positionUs = positionMs * 1000;
        emit m_adaptor->Seeked(positionMs * 1000);
    }

private:
    void notify(const QVariantMap &changed)
    {
        auto msg = QDBusMessage::createSignal(Mpris::kPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                              QStringLiteral("PropertiesChanged"));
        msg << Mpris::kPlayerInterface << changed << QStringList();
        m_conn.send(msg);
    }

    QDBusConnection m_conn;
    QString m_service;
    QObject m_root;
    FakePlayerAdaptor *m_adaptor = nullptr;
};
