#include "sync/LyricSession.h"

#include <QPointer>

#include "lyrics/SpicyLyricsProvider.h"
#include "sync/LyricSyncEngine.h"

LyricSession::LyricSession(LyricProvider *provider, LyricSyncEngine *engine, QObject *parent)
    : QObject(parent), m_provider(provider), m_engine(engine)
{
    m_retryTimer.setSingleShot(true);
    connect(&m_retryTimer, &QTimer::timeout, this, [this] { startLookup(); });
}

QString LyricSession::identity(const TrackSnapshot &snapshot)
{
    return QStringList{snapshot.service, snapshot.trackId, snapshot.title, snapshot.artist}.join(u'\x1f');
}

void LyricSession::onActiveTrackChanged(const TrackSnapshot &snapshot)
{
    if (!snapshot.isValid()) {
        cancelPending();
        m_identity.clear();
        m_track = {};
        m_engine->setLyrics({});
        m_engine->setTrack(snapshot);
        setStatus(Status::NoPlayer);
        return;
    }

    const QString id = identity(snapshot);
    const qint64 previousLength = m_track.lengthMs;
    m_track = snapshot;
    if (id == m_identity) {
        // The length arrived after a lookup that found nothing: matching needs it, so ask again.
        const bool failed = m_status == Status::NoLyrics || m_status == Status::Unavailable;
        if (failed && m_pendingId == 0 && previousLength <= 0 && snapshot.lengthMs > 0) {
            m_engine->setTrack(snapshot);
            m_retried = false;
            startLookup();
            return;
        }
        m_engine->setTrack(snapshot); // position or state update only
        return;
    }

    cancelPending();
    m_identity = id;
    m_retried = false;
    m_engine->setLyrics({});
    m_engine->setTrack(snapshot);
    if (snapshot.title.trimmed().isEmpty()) {
        setStatus(Status::NoLyrics, QStringLiteral("the player did not report a title"));
        return;
    }
    startLookup();
}

void LyricSession::startLookup()
{
    setStatus(Status::Searching);
    const quint64 token = ++m_generation;
    LyricQuery query{m_track.title, m_track.artist, m_track.album, m_track.lengthMs};
    query.localUrl = m_track.url;
    query.spotifyTrackId = SpicyLyricsProvider::spotifyTrackId(m_track.trackId, m_track.url);
    m_pendingId = m_provider->fetch(query, [self = QPointer<LyricSession>(this), token](const LyricResult &r) {
        if (self)
            self->onResult(token, r);
    });
}

void LyricSession::onResult(quint64 token, const LyricResult &result)
{
    if (token != m_generation)
        return; // response for an earlier track or lookup
    m_pendingId = 0;

    switch (result.kind) {
    case LyricResult::Kind::Timed:
        m_engine->setLyrics(result.document);
        setStatus(Status::Ready);
        break;
    case LyricResult::Kind::NoMatch:
        setStatus(Status::NoLyrics, result.message);
        break;
    case LyricResult::Kind::InvalidResponse:
        setStatus(Status::Unavailable, result.message);
        break;
    case LyricResult::Kind::ProviderError:
        if (!m_retried && result.retryAfterSec <= kMaxRetryAfterSec) {
            m_retried = true;
            setStatus(Status::Searching, result.message);
            // Wait past the provider's own back-off window, which adds up to 30 % jitter.
            m_retryTimer.start(std::max(1, result.retryAfterSec) * 1300 + 100);
        } else {
            setStatus(Status::Unavailable, result.message);
        }
        break;
    }
}

void LyricSession::cancelPending()
{
    m_retryTimer.stop();
    if (m_pendingId)
        m_provider->cancel(m_pendingId);
    m_pendingId = 0;
    ++m_generation;
}

void LyricSession::setStatus(Status status, const QString &message)
{
    if (status == m_status && message == m_message)
        return;
    m_status = status;
    m_message = message;
    emit statusChanged(status, message);
}
