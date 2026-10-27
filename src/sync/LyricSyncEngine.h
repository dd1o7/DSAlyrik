#pragma once

#include <QObject>
#include <QTimer>
#include <functional>

#include "lyrics/LyricTypes.h"
#include "playback/MprisPlayer.h"

// Maps playback position to the active line and word.
//
// Power use: the engine runs no timer unless playback is Playing and lyrics
// are loaded. While playing it sleeps until the next word or line boundary;
// only continuous-frame mode (for styles that animate between boundaries)
// ticks at ~30 Hz.
class LyricSyncEngine : public QObject {
    Q_OBJECT
public:
    using Clock = std::function<qint64()>;
    struct Location {
        int line = -1; // -1 before the first line
        int word = -1; // -1 when no word of `line` is being sung
        bool operator==(const Location &) const = default;
    };

    explicit LyricSyncEngine(QObject *parent = nullptr, Clock clock = &Mpris::monotonicNowMs);

    void setTrack(const TrackSnapshot &snapshot); // resynchronizes position and state
    void setLyrics(const LyricDocument &document); // word timing is synthesized where missing
    void setPlaybackState(PlaybackState state);
    void setContinuousFrames(bool continuous);
    // Show lyrics this far ahead of the reported playhead, compensating for
    // player-reporting and display latency.
    void setLeadMs(qint64 leadMs);
    // While suspended (lyrics hidden) the clock keeps time but no timer runs.
    void setSuspended(bool suspended);

    const LyricDocument &lyrics() const { return m_document; }
    qint64 positionMs() const;
    qint64 displayPositionMs() const { return positionMs() + m_leadMs; }
    Location location() const { return m_location; }
    bool isTicking() const { return m_timer.isActive(); }
    int nextWakeMs() const { return m_timer.isActive() ? m_timer.interval() : -1; }

    static Location locate(const LyricDocument &document, qint64 positionMs);

    static constexpr int kContinuousIntervalMs = 33;
    static constexpr qint64 kResyncToleranceMs = 250;

signals:
    void frameChanged(qint64 positionMs, int lineIndex, int wordIndex);
    void lyricsChanged();

private:
    void update(bool forceEmit);
    void schedule(qint64 positionMs);
    qint64 nextBoundaryAfter(qint64 positionMs) const;

    Clock m_now;
    QTimer m_timer;
    PositionClock m_clock;
    qint64 m_lengthMs = 0;
    PlaybackState m_state = PlaybackState::Stopped;
    LyricDocument m_document;
    Location m_location;
    bool m_continuous = false;
    qint64 m_leadMs = 0;
    bool m_suspended = false;
};
