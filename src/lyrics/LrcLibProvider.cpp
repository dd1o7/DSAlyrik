#include "lyrics/LrcLibProvider.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTimer>
#include <QUrlQuery>
#include <cmath>
#include <limits>

#include "lyrics/LrcParser.h"
#include "lyrics/PlainLyricsTimer.h"
#include "playback/MprisPlayer.h"
#include "lyrics/TrackNameNormalizer.h"

namespace {

constexpr int kDefaultRetryAfterSec = 30;
constexpr qsizetype kMaxCacheEntries = 256;
constexpr qint64 kSearchDurationToleranceMs = 3000;

QString normalized(const QString &text)
{
    static const QRegularExpression whitespace(QStringLiteral(R"(\s+)"));
    return text.simplified().toCaseFolded().replace(whitespace, QStringLiteral(" "));
}

qint64 durationSeconds(qint64 durationMs) { return std::llround(durationMs / 1000.0); }

LyricResult makeResult(LyricResult::Kind kind, const QString &message)
{
    LyricResult r;
    r.kind = kind;
    r.message = message;
    return r;
}

} // namespace

LrcLibProvider::LrcLibProvider(const QUrl &baseUrl, QObject *parent) : LyricProvider(parent), m_baseUrl(baseUrl) {}

QByteArray LrcLibProvider::userAgent()
{
    // LRCLIB asks clients to name themselves, their version and a homepage (docs/lyrics-providers.md).
    return QByteArrayLiteral("DSAlyrik/" DSALYRIK_VERSION " (https://github.com/dd1o7/DSAlyrik)");
}

QString LrcLibProvider::cacheKey(const LyricQuery &query)
{
    return normalized(query.title) + u'\x1f' + normalized(query.artist) + u'\x1f'
           + QString::number(durationSeconds(query.durationMs));
}

RequestId LrcLibProvider::fetch(const LyricQuery &query, Completion completion)
{
    const RequestId id = m_nextId++;
    const QString key = cacheKey(query);
    m_pending.insert(id, {nullptr, std::move(completion), key});

    if (query.title.trimmed().isEmpty() || query.artist.trimmed().isEmpty()) {
        deliverLater(id, makeResult(LyricResult::Kind::NoMatch, QStringLiteral("title or artist missing")));
        return id;
    }
    if (const auto cached = m_cache.constFind(key); cached != m_cache.cend()) {
        deliverLater(id, *cached);
        return id;
    }
    if (const qint64 now = Mpris::monotonicNowMs(); now < m_blockedUntilMs) {
        LyricResult r = makeResult(LyricResult::Kind::ProviderError, QStringLiteral("rate limited by LRCLIB"));
        r.retryAfterSec = int((m_blockedUntilMs - now + 999) / 1000);
        deliverLater(id, r);
        return id;
    }

    m_pending[id].query = query;
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("track_name"), query.title.trimmed());
    params.addQueryItem(QStringLiteral("artist_name"), query.artist.trimmed());
    if (!query.album.trimmed().isEmpty())
        params.addQueryItem(QStringLiteral("album_name"), query.album.trimmed());
    if (const qint64 seconds = durationSeconds(query.durationMs); seconds >= 1 && seconds <= 3600)
        params.addQueryItem(QStringLiteral("duration"), QString::number(seconds));
    startRequest(id, QStringLiteral("/api/get"), params);
    return id;
}

void LrcLibProvider::startRequest(RequestId id, const QString &path, const QUrlQuery &params)
{
    QUrl url = m_baseUrl.resolved(QUrl(path));
    url.setQuery(params);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    request.setTransferTimeout(m_timeoutMs);
    QNetworkReply *reply = m_network.get(request);
    m_pending[id].reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, id] { handleReply(id); });
}

void LrcLibProvider::cancel(RequestId id)
{
    const Pending pending = m_pending.take(id);
    if (pending.reply) {
        pending.reply->disconnect(this);
        pending.reply->abort();
        pending.reply->deleteLater();
    }
}

void LrcLibProvider::deliverLater(RequestId id, const LyricResult &result)
{
    QTimer::singleShot(0, this, [this, id, result] {
        const Pending pending = m_pending.take(id); // empty if cancelled meanwhile
        if (pending.completion)
            pending.completion(result);
    });
}

void LrcLibProvider::handleReply(RequestId id)
{
    Pending pending = m_pending.take(id);
    if (!pending.reply)
        return;
    QNetworkReply *reply = pending.reply;
    reply->deleteLater();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 404 && !pending.searched) {
        // Sequential, never parallel: at most one follow-up request per lookup.
        pending.searched = true;
        pending.reply = nullptr;
        m_pending.insert(id, pending);
        QUrlQuery params;
        params.addQueryItem(QStringLiteral("track_name"), TrackNameNormalizer::cleanTitle(pending.query.title));
        params.addQueryItem(QStringLiteral("artist_name"), TrackNameNormalizer::primaryArtist(pending.query.artist));
        startRequest(id, QStringLiteral("/api/search"), params);
        return;
    }

    const LyricResult result = interpret(reply, pending);
    if (result.kind == LyricResult::Kind::Timed || result.kind == LyricResult::Kind::NoMatch) {
        if (m_cache.size() >= kMaxCacheEntries)
            m_cache.clear();
        m_cache.insert(pending.cacheKey, result);
    }
    if (pending.completion)
        pending.completion(result);
}

LyricResult LrcLibProvider::interpret(QNetworkReply *reply, const Pending &pending)
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (status == 429 || status == 503) {
        bool ok = false;
        int retryAfter = reply->rawHeader("Retry-After").trimmed().toInt(&ok);
        if (!ok || retryAfter <= 0)
            retryAfter = kDefaultRetryAfterSec;
        const double jitter = 1.0 + QRandomGenerator::global()->bounded(0.3);
        m_blockedUntilMs = Mpris::monotonicNowMs() + qint64(retryAfter * 1000 * jitter); // immune to clock changes
        LyricResult r = makeResult(LyricResult::Kind::ProviderError, status == 429
                                                                         ? QStringLiteral("rate limited by LRCLIB")
                                                                         : QStringLiteral("LRCLIB is busy"));
        r.retryAfterSec = retryAfter;
        return r;
    }
    if (status == 404)
        return makeResult(LyricResult::Kind::NoMatch, QStringLiteral("no LRCLIB record"));
    if (status != 200) {
        const QString detail = status ? QStringLiteral("HTTP %1").arg(status) : reply->errorString();
        return makeResult(LyricResult::Kind::ProviderError, detail);
    }

    QJsonParseError jsonError;
    const QJsonDocument json = QJsonDocument::fromJson(reply->readAll(), &jsonError);
    if (jsonError.error != QJsonParseError::NoError)
        return makeResult(LyricResult::Kind::InvalidResponse, QStringLiteral("response is not JSON"));
    if (pending.searched) {
        if (!json.isArray())
            return makeResult(LyricResult::Kind::InvalidResponse, QStringLiteral("search response is not a list"));
        const auto best = bestCandidate(json.array(), pending.query);
        return best ? fromRecord(*best, pending.query.durationMs)
                    : makeResult(LyricResult::Kind::NoMatch, QStringLiteral("no LRCLIB candidate"));
    }
    if (!json.isObject())
        return makeResult(LyricResult::Kind::InvalidResponse, QStringLiteral("response is not a JSON object"));
    return fromRecord(json.object(), pending.query.durationMs);
}

LyricResult LrcLibProvider::fromRecord(const QJsonObject &record, qint64 durationMs)
{
    if (record.value(QStringLiteral("instrumental")).toBool())
        return makeResult(LyricResult::Kind::NoMatch, QStringLiteral("instrumental"));
    const QString synced = record.value(QStringLiteral("syncedLyrics")).toString();
    if (synced.trimmed().isEmpty()) {
        // Unsynced lyrics only: spread them over the track, clearly marked as estimated.
        LyricDocument estimated =
            PlainLyricsTimer::estimate(record.value(QStringLiteral("plainLyrics")).toString(), durationMs);
        if (estimated.lines.isEmpty())
            return makeResult(LyricResult::Kind::NoMatch, QStringLiteral("no synchronized lyrics"));
        LyricResult r = makeResult(LyricResult::Kind::Timed, QStringLiteral("estimated timing"));
        estimated.title = record.value(QStringLiteral("trackName")).toString();
        estimated.artist = record.value(QStringLiteral("artistName")).toString();
        estimated.provider = QStringLiteral("LRCLIB");
        r.document = std::move(estimated);
        return r;
    }

    auto parsed = LrcParser::parse(synced);
    if (auto *error = std::get_if<ParseError>(&parsed))
        return makeResult(LyricResult::Kind::InvalidResponse, error->message);

    LyricResult r = makeResult(LyricResult::Kind::Timed, {});
    r.document = std::get<LyricDocument>(std::move(parsed));
    r.document.provider = QStringLiteral("LRCLIB");
    if (r.document.title.isEmpty())
        r.document.title = record.value(QStringLiteral("trackName")).toString();
    if (r.document.artist.isEmpty())
        r.document.artist = record.value(QStringLiteral("artistName")).toString();
    return r;
}

std::optional<QJsonObject> LrcLibProvider::bestCandidate(const QJsonArray &records, const LyricQuery &query)
{
    using N = TrackNameNormalizer;
    const QString wantTitle = N::comparable(N::cleanTitle(query.title));
    const QString wantArtist = N::comparable(N::primaryArtist(query.artist));

    std::optional<QJsonObject> best;
    qint64 bestScore = std::numeric_limits<qint64>::min();
    for (const QJsonValue &value : records) {
        const QJsonObject rec = value.toObject();
        const bool synced = !rec.value(QStringLiteral("syncedLyrics")).toString().trimmed().isEmpty();
        const bool plain = !rec.value(QStringLiteral("plainLyrics")).toString().trimmed().isEmpty();
        if (rec.value(QStringLiteral("instrumental")).toBool() || !(synced || (plain && query.durationMs > 0)))
            continue;
        if (N::comparable(N::cleanTitle(rec.value(QStringLiteral("trackName")).toString())) != wantTitle)
            continue;
        const qint64 durationDiff = query.durationMs > 0
            ? std::llabs(qint64(rec.value(QStringLiteral("duration")).toDouble() * 1000) - query.durationMs)
            : 0;
        if (durationDiff > kSearchDurationToleranceMs)
            continue;
        const QString artist = N::comparable(rec.value(QStringLiteral("artistName")).toString());
        const bool artistMatches = !wantArtist.isEmpty() && (artist.contains(wantArtist) || wantArtist.contains(artist));
        if (!artistMatches && (query.durationMs <= 0 || durationDiff > 1000))
            continue; // a different artist is only plausible when the length agrees closely
        const qint64 score = (synced ? 1'000'000 : 0) + (artistMatches ? 100'000 : 0) - durationDiff;
        if (score > bestScore) {
            bestScore = score;
            best = rec;
        }
    }
    return best;
}
