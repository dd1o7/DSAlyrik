#include "playback/MprisPlayer.h"

#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <QDBusVariant>

void PositionClock::reset(qint64 positionMs, qint64 nowMs, bool playing, double playbackRate)
{
    basePositionMs = std::max<qint64>(0, positionMs);
    baseTimeMs = nowMs;
    running = playing;
    rate = playbackRate > 0 ? playbackRate : 1.0;
}

qint64 PositionClock::positionAt(qint64 nowMs, qint64 lengthMs) const
{
    qint64 pos = basePositionMs;
    if (running)
        pos += static_cast<qint64>((nowMs - baseTimeMs) * rate);
    return lengthMs > 0 ? std::min(pos, lengthMs) : pos;
}

namespace Mpris {

qint64 monotonicNowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

PlaybackState parseStatus(const QString &status)
{
    if (status == u"Playing")
        return PlaybackState::Playing;
    if (status == u"Paused")
        return PlaybackState::Paused;
    return PlaybackState::Stopped;
}

QVariantMap toVariantMap(const QVariant &value)
{
    if (value.canConvert<QDBusArgument>())
        return qdbus_cast<QVariantMap>(value.value<QDBusArgument>());
    return value.toMap();
}

static QStringList toStringList(const QVariant &value)
{
    if (value.canConvert<QDBusArgument>())
        return qdbus_cast<QStringList>(value.value<QDBusArgument>());
    if (value.metaType().id() == QMetaType::QString)
        return {value.toString()};
    return value.toStringList();
}

void applyMetadata(TrackSnapshot &snapshot, const QVariantMap &metadata)
{
    const QVariant id = metadata.value(QStringLiteral("mpris:trackid"));
    snapshot.trackId = id.canConvert<QDBusObjectPath>() && id.metaType() == QMetaType::fromType<QDBusObjectPath>()
                           ? id.value<QDBusObjectPath>().path()
                           : id.toString();
    snapshot.title = metadata.value(QStringLiteral("xesam:title")).toString();
    snapshot.artist = toStringList(metadata.value(QStringLiteral("xesam:artist"))).join(QStringLiteral(", "));
    snapshot.album = metadata.value(QStringLiteral("xesam:album")).toString();
    snapshot.lengthMs = std::max<qint64>(0, metadata.value(QStringLiteral("mpris:length")).toLongLong() / 1000);
    snapshot.artUrl = QUrl(metadata.value(QStringLiteral("mpris:artUrl")).toString());
    snapshot.url = QUrl(metadata.value(QStringLiteral("xesam:url")).toString());
}

} // namespace Mpris

MprisPlayer::MprisPlayer(const QString &service, const QDBusConnection &bus, QObject *parent)
    : QObject(parent), m_bus(bus)
{
    m_track.service = service;
    m_bus.connect(service, Mpris::kPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                  QStringLiteral("PropertiesChanged"), this,
                  SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)));
    m_bus.connect(service, Mpris::kPath, Mpris::kPlayerInterface, QStringLiteral("Seeked"), this,
                  SLOT(onSeeked(qlonglong)));
}

TrackSnapshot MprisPlayer::snapshot() const
{
    TrackSnapshot s = m_track;
    s.positionMs = m_clock.positionAt(Mpris::monotonicNowMs(), m_track.lengthMs);
    return s;
}

void MprisPlayer::refresh()
{
    auto msg = QDBusMessage::createMethodCall(m_track.service, Mpris::kPath,
                                              QStringLiteral("org.freedesktop.DBus.Properties"),
                                              QStringLiteral("GetAll"));
    msg << Mpris::kPlayerInterface;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(msg, 2000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<QVariantMap> reply = *w;
        if (!reply.isError())
            applyProperties(reply.value());
    });
}

void MprisPlayer::resyncPosition()
{
    auto msg = QDBusMessage::createMethodCall(m_track.service, Mpris::kPath,
                                              QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    msg << Mpris::kPlayerInterface << QStringLiteral("Position");
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(msg, 1000), this);
    const qint64 sentAt = Mpris::monotonicNowMs();
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, sentAt](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<QDBusVariant> reply = *w;
        if (reply.isError())
            return;
        // The player sampled its position somewhere between send and receive; use the midpoint.
        const qint64 now = Mpris::monotonicNowMs();
        const qint64 sampledAt = (sentAt + now) / 2;
        const qint64 reported = reply.value().variant().toLongLong() / 1000;
        const qint64 expected = m_clock.positionAt(sampledAt, m_track.lengthMs);
        if (std::llabs(reported - expected) <= kDriftToleranceMs)
            return; // in sync: no signal, no redraw
        m_clock.reset(reported, sampledAt, m_clock.running, m_clock.rate);
        emit changed();
    });
}

void MprisPlayer::onPropertiesChanged(const QString &interface, const QVariantMap &changedProps, const QStringList &)
{
    if (interface != Mpris::kPlayerInterface)
        return;
    applyProperties(changedProps);
    // Position is not signaled, so resynchronize it after track or state changes.
    if (changedProps.contains(QStringLiteral("Metadata")) || changedProps.contains(QStringLiteral("PlaybackStatus")))
        refresh();
}

void MprisPlayer::onSeeked(qlonglong positionUs)
{
    m_clock.reset(positionUs / 1000, Mpris::monotonicNowMs(), m_clock.running, m_clock.rate);
    emit changed();
}

void MprisPlayer::applyProperties(const QVariantMap &props)
{
    const qint64 now = Mpris::monotonicNowMs();
    const qint64 currentPos = m_clock.positionAt(now, m_track.lengthMs);
    const TrackSnapshot before = m_track;

    if (props.contains(QStringLiteral("Metadata")))
        Mpris::applyMetadata(m_track, Mpris::toVariantMap(props.value(QStringLiteral("Metadata"))));
    if (props.contains(QStringLiteral("PlaybackStatus")))
        m_track.state = Mpris::parseStatus(props.value(QStringLiteral("PlaybackStatus")).toString());
    const double rate = props.value(QStringLiteral("Rate"), m_clock.rate).toDouble();

    const bool trackChanged = m_track.trackId != before.trackId || m_track.title != before.title
                              || m_track.artist != before.artist;
    const bool stateChanged = m_track.state != before.state;
    const bool playing = m_track.state == PlaybackState::Playing;

    if (props.contains(QStringLiteral("Position")))
        m_clock.reset(props.value(QStringLiteral("Position")).toLongLong() / 1000, now, playing, rate);
    else if (trackChanged)
        m_clock.reset(0, now, playing, rate);
    else
        m_clock.reset(currentPos, now, playing, rate);

    if (trackChanged || stateChanged)
        m_lastChangedMs = now;
    emit changed();
}
