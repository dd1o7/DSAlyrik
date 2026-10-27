#pragma once

#include <QHash>
#include <QNetworkAccessManager>
#include <QUrl>
#include <functional>

#include "lyrics/LyricProvider.h"

class QJsonObject;
class QNetworkReply;

// Lyrics from the Spicy Lyrics API (developers.spicylyrics.org): the best sync
// for a Spotify track, community TTML first, then Apple Music, then Spotify.
// Syllable syncs give real word timing. Needs the user's own secret key; the
// provider and community credits must be shown with the lyrics.
class SpicyLyricsProvider : public LyricProvider {
    Q_OBJECT
public:
    using KeySource = std::function<QString()>;
    explicit SpicyLyricsProvider(const QUrl &baseUrl = QUrl(QStringLiteral("https://api.spicylyrics.org")),
                                 KeySource key = {}, QObject *parent = nullptr);

    RequestId fetch(const LyricQuery &query, Completion completion) override;
    void cancel(RequestId id) override;
    void setTimeoutMs(int ms) { m_timeoutMs = ms; }

    // From MPRIS trackid (/com/spotify/track/ID, spotify:track:ID) or an open.spotify.com URL.
    static QString spotifyTrackId(const QString &mprisTrackId, const QUrl &url);
    static LyricResult parse(const QJsonObject &body, qint64 durationMs);

private:
    struct Pending {
        QNetworkReply *reply = nullptr;
        Completion completion;
        QString trackId;
        qint64 durationMs = 0;
    };
    void deliverLater(RequestId id, const LyricResult &result);

    QNetworkAccessManager m_network;
    QUrl m_baseUrl;
    KeySource m_key;
    int m_timeoutMs = 8000;
    RequestId m_next = 0;
    QHash<RequestId, Pending> m_pending;
    QHash<QString, LyricResult> m_cache; // in memory only
    qint64 m_blockedUntilMs = 0;
};
