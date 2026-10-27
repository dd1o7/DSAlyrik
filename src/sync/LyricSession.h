#pragma once

#include <QObject>
#include <QTimer>

#include "lyrics/LyricProvider.h"
#include "playback/MprisPlayer.h"

class LyricSyncEngine;

// Connects the active MPRIS track to lyric lookup and the sync engine.
// A track change cancels the pending lookup and clears old lyrics at once;
// responses for any earlier track are ignored.
class LyricSession : public QObject {
    Q_OBJECT
public:
    enum class Status { NoPlayer, Searching, Ready, NoLyrics, Unavailable };
    Q_ENUM(Status)

    LyricSession(LyricProvider *provider, LyricSyncEngine *engine, QObject *parent = nullptr);

    Status status() const { return m_status; }
    QString statusMessage() const { return m_message; }

    static constexpr int kMaxRetryAfterSec = 5; // longer back-offs wait for the next track

public slots:
    void onActiveTrackChanged(const TrackSnapshot &snapshot);

signals:
    void statusChanged(LyricSession::Status status, const QString &message);

private:
    static QString identity(const TrackSnapshot &snapshot);
    void startLookup();
    void onResult(quint64 token, const LyricResult &result);
    void cancelPending();
    void setStatus(Status status, const QString &message = {});

    LyricProvider *m_provider;
    LyricSyncEngine *m_engine;
    QTimer m_retryTimer;
    TrackSnapshot m_track;
    QString m_identity;
    RequestId m_pendingId = 0;
    quint64 m_generation = 0; // identifies the lookup whose result is still wanted
    bool m_retried = false;
    Status m_status = Status::NoPlayer;
    QString m_message;
};
