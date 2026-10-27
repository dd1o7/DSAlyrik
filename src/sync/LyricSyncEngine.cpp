#include "sync/LyricSyncEngine.h"

#include <algorithm>
#include <cstdlib>

#include "lyrics/WordTimingSynthesizer.h"

LyricSyncEngine::LyricSyncEngine(QObject *parent, Clock clock) : QObject(parent), m_now(std::move(clock))
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this] { update(m_continuous); }); // continuous: every tick draws
}

void LyricSyncEngine::setTrack(const TrackSnapshot &snapshot)
{
    // Redraw only on a seek-sized jump or a state change; routine resyncs stay silent.
    const bool jumped = std::abs(snapshot.positionMs - positionMs()) > kResyncToleranceMs;
    const bool stateChanged = snapshot.state != m_state;
    m_lengthMs = snapshot.lengthMs;
    m_state = snapshot.state;
    m_clock.reset(snapshot.positionMs, m_now(), m_state == PlaybackState::Playing);
    update(jumped || stateChanged);
}

void LyricSyncEngine::setLyrics(const LyricDocument &document)
{
    m_document = WordTimingSynthesizer::expand(document);
    m_location = {};
    emit lyricsChanged();
    update(true);
}

void LyricSyncEngine::setPlaybackState(PlaybackState state)
{
    const qint64 position = positionMs();
    m_state = state;
    m_clock.reset(position, m_now(), m_state == PlaybackState::Playing);
    update(true);
}

void LyricSyncEngine::setSuspended(bool suspended)
{
    if (suspended == m_suspended)
        return;
    m_suspended = suspended;
    update(true); // resuming draws the current frame at once; suspending stops the timer
}

void LyricSyncEngine::setLeadMs(qint64 leadMs)
{
    m_leadMs = leadMs;
    update(true);
}

void LyricSyncEngine::setContinuousFrames(bool continuous)
{
    m_continuous = continuous;
    update(false);
}

qint64 LyricSyncEngine::positionMs() const { return m_clock.positionAt(m_now(), m_lengthMs); }

LyricSyncEngine::Location LyricSyncEngine::locate(const LyricDocument &document, qint64 positionMs)
{
    const auto &lines = document.lines;
    const auto lineIt = std::upper_bound(lines.cbegin(), lines.cend(), positionMs,
                                         [](qint64 pos, const LyricLine &l) { return pos < l.startMs; });
    const int line = int(lineIt - lines.cbegin()) - 1;
    if (line < 0)
        return {};
    const LyricLine &current = lines[line];
    if (positionMs >= current.endMs)
        return {line, -1};
    const auto wordIt = std::upper_bound(current.words.cbegin(), current.words.cend(), positionMs,
                                         [](qint64 pos, const LyricWord &w) { return pos < w.startMs; });
    const int word = int(wordIt - current.words.cbegin()) - 1;
    if (word < 0 || positionMs >= current.words[word].endMs)
        return {line, -1};
    return {line, word};
}

void LyricSyncEngine::update(bool forceEmit)
{
    const qint64 position = positionMs() + m_leadMs; // displayed position
    const Location location = locate(m_document, position);
    const bool changed = location != m_location;
    m_location = location;
    if (forceEmit || changed)
        emit frameChanged(position, location.line, location.word);
    schedule(position);
}

void LyricSyncEngine::schedule(qint64 positionMs)
{
    const qint64 boundary = nextBoundaryAfter(positionMs);
    if (m_suspended || m_state != PlaybackState::Playing || m_document.lines.isEmpty() || boundary < 0) {
        m_timer.stop(); // idle: no wakeups until playback or lyrics change
        return;
    }
    if (m_continuous) {
        m_timer.setTimerType(Qt::CoarseTimer);
        m_timer.start(kContinuousIntervalMs);
        return;
    }
    const double rate = m_clock.rate > 0 ? m_clock.rate : 1.0;
    const qint64 delay = std::max<qint64>(1, qint64((boundary - positionMs) / rate));
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.start(int(std::min<qint64>(delay, 60'000)));
}

qint64 LyricSyncEngine::nextBoundaryAfter(qint64 positionMs) const
{
    // Line and word times are ascending, so the first time past the position is the next boundary.
    for (qsizetype i = std::max(0, m_location.line); i < m_document.lines.size(); ++i) {
        const LyricLine &line = m_document.lines[i];
        if (line.startMs > positionMs)
            return line.startMs;
        for (const LyricWord &word : line.words) {
            if (word.startMs > positionMs)
                return word.startMs;
            if (word.endMs > positionMs)
                return word.endMs;
        }
        if (line.endMs > positionMs)
            return line.endMs;
    }
    return -1;
}
