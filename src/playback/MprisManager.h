#pragma once

#include <QDBusServiceWatcher>
#include <QHash>
#include <QTimer>
#include <QObject>

#include "playback/MprisPlayer.h"

// Discovers org.mpris.MediaPlayer2.* services and selects the active one:
// the preferred player if set, else the most recently changed playing player,
// else the current selection while it exists.
class MprisManager : public QObject {
    Q_OBJECT
public:
    explicit MprisManager(const QDBusConnection &bus = QDBusConnection::sessionBus(), QObject *parent = nullptr);

    void start();
    QStringList players() const;
    QString activeService() const { return m_active; }
    TrackSnapshot activeSnapshot() const; // invalid when no player is selected
    void setPreferredService(const QString &service); // empty = automatic
    // While the active player is Playing, its position is re-read this often to
    // catch drift the player never signals (buffering stalls, clock skew).
    void setResyncIntervalMs(int ms) { m_resyncTimer.setInterval(ms); }
    bool isResyncing() const { return m_resyncTimer.isActive(); }
    void setResyncEnabled(bool enabled); // off while no lyrics are shown
    static constexpr int kResyncIntervalMs = 2000;

signals:
    void playerListChanged();
    void activeTrackChanged(const TrackSnapshot &snapshot);

private:
    void addPlayer(const QString &service);
    void removePlayer(const QString &service);
    void reselect(MprisPlayer *source);

    QDBusConnection m_bus;
    QDBusServiceWatcher m_watcher;
    QHash<QString, MprisPlayer *> m_players;
    QString m_active;
    QString m_preferred;
    QTimer m_resyncTimer;
    bool m_resyncEnabled = true;
};
