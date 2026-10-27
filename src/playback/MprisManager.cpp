#include "playback/MprisManager.h"

#include <QDBusConnectionInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

namespace {
// playerctld re-publishes other players; following it would duplicate them.
bool isFollowedService(const QString &name)
{
    return name.startsWith(Mpris::kServicePrefix) && name != Mpris::kServicePrefix + QStringLiteral("playerctld");
}
} // namespace

MprisManager::MprisManager(const QDBusConnection &bus, QObject *parent) : QObject(parent), m_bus(bus)
{
    m_resyncTimer.setInterval(kResyncIntervalMs);
    m_resyncTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_resyncTimer, &QTimer::timeout, this, [this] {
        if (MprisPlayer *player = m_players.value(m_active))
            player->resyncPosition();
    });
}

void MprisManager::start()
{
    m_watcher.setConnection(m_bus);
    m_watcher.setWatchMode(QDBusServiceWatcher::WatchForOwnerChange);
    m_watcher.setWatchedServices({QStringLiteral("org.mpris.MediaPlayer2*")});
    connect(&m_watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString &name, const QString &, const QString &newOwner) {
                removePlayer(name); // an owner change means a new player instance
                if (!newOwner.isEmpty())
                    addPlayer(name);
            });

    auto *watcher = new QDBusPendingCallWatcher(m_bus.interface()->asyncCall(QStringLiteral("ListNames")), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<QStringList> reply = *w;
        if (reply.isError())
            return;
        for (const QString &name : reply.value())
            addPlayer(name);
    });
}

QStringList MprisManager::players() const
{
    QStringList names = m_players.keys();
    names.sort();
    return names;
}

TrackSnapshot MprisManager::activeSnapshot() const
{
    const MprisPlayer *player = m_players.value(m_active);
    return player ? player->snapshot() : TrackSnapshot{};
}

void MprisManager::setResyncEnabled(bool enabled)
{
    m_resyncEnabled = enabled;
    reselect(nullptr);
}

void MprisManager::setPreferredService(const QString &service)
{
    m_preferred = service;
    reselect(nullptr);
}

void MprisManager::addPlayer(const QString &service)
{
    if (!isFollowedService(service) || m_players.contains(service))
        return;
    auto *player = new MprisPlayer(service, m_bus, this);
    m_players.insert(service, player);
    connect(player, &MprisPlayer::changed, this, [this, player] { reselect(player); });
    player->refresh();
    emit playerListChanged();
}

void MprisManager::removePlayer(const QString &service)
{
    MprisPlayer *player = m_players.take(service);
    if (!player)
        return;
    player->deleteLater();
    emit playerListChanged();
    reselect(nullptr);
}

void MprisManager::reselect(MprisPlayer *source)
{
    QString next;
    if (m_players.contains(m_preferred)) {
        next = m_preferred;
    } else {
        qint64 newest = -1;
        for (const MprisPlayer *p : std::as_const(m_players)) {
            if (p->snapshot().state == PlaybackState::Playing && p->lastChangedMs() > newest) {
                newest = p->lastChangedMs();
                next = p->service();
            }
        }
        if (next.isEmpty() && m_players.contains(m_active))
            next = m_active;
        if (next.isEmpty()) {
            for (const MprisPlayer *p : std::as_const(m_players)) {
                if (p->lastChangedMs() > newest) {
                    newest = p->lastChangedMs();
                    next = p->service();
                }
            }
        }
    }

    const bool switched = next != m_active;
    m_active = next;
    // Poll only while something is actually playing: idle and paused stay timer-free.
    const MprisPlayer *active = m_players.value(m_active);
    const bool playing = m_resyncEnabled && active && active->snapshot().state == PlaybackState::Playing;
    if (playing && !m_resyncTimer.isActive())
        m_resyncTimer.start();
    else if (!playing)
        m_resyncTimer.stop();
    if (switched || (source && source->service() == m_active))
        emit activeTrackChanged(activeSnapshot());
}
