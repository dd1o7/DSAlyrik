#pragma once

#include <QObject>
#include <QUrl>
#include <functional>

#include "lyrics/LyricTypes.h"

struct LyricQuery {
    QString title;
    QString artist;
    QString album;
    qint64 durationMs = 0; // 0 when unknown
    QUrl localUrl;         // the playing file, when the player reports one (xesam:url)
    QString spotifyTrackId; // 22-character Spotify id, when the player is Spotify
};

struct LyricResult {
    enum class Kind { Timed, NoMatch, ProviderError, InvalidResponse };
    Kind kind = Kind::NoMatch;
    LyricDocument document; // set only for Timed
    QString message;
    int retryAfterSec = 0;  // set when the provider asked us to back off
};

using RequestId = quint64;
using Completion = std::function<void(const LyricResult &)>;

// Asynchronous lyric source. The completion runs exactly once on the
// provider's thread, never synchronously inside fetch(), and never after
// cancel() for that request.
class LyricProvider : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual RequestId fetch(const LyricQuery &query, Completion completion) = 0;
    virtual void cancel(RequestId id) = 0;
};
