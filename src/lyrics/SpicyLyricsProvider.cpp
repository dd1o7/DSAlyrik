#include "lyrics/SpicyLyricsProvider.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTimer>
#include <cmath>

#include "lyrics/LrcLibProvider.h"
#include "lyrics/PlainLyricsTimer.h"
#include "playback/MprisPlayer.h"

namespace {

constexpr int kDefaultRetryAfterSec = 30;

qint64 ms(const QJsonValue &seconds) { return std::llround(seconds.toDouble() * 1000.0); }

LyricResult result(LyricResult::Kind kind, const QString &message)
{
    LyricResult r;
    r.kind = kind;
    r.message = message;
    return r;
}

QString providerName(const QString &source)
{
    if (source == u"spicy_lyrics")
        return QStringLiteral("Spicy Lyrics community");
    if (source == u"apple_music")
        return QStringLiteral("Apple Music via Spicy Lyrics");
    if (source == u"spotify")
        return QStringLiteral("Spotify via Spicy Lyrics");
    return source.isEmpty() ? QStringLiteral("Spicy Lyrics") : source + QStringLiteral(" via Spicy Lyrics");
}

// Syllables -> words: a syllable with IsPartOfWord continues into the next one.
LyricLine lineFromSyllables(const QJsonObject &lead)
{
    LyricLine line;
    line.startMs = ms(lead.value(QStringLiteral("StartTime")));
    line.endMs = ms(lead.value(QStringLiteral("EndTime")));
    LyricWord word;
    bool open = false;
    for (const QJsonValue &v : lead.value(QStringLiteral("Syllables")).toArray()) {
        const QJsonObject s = v.toObject();
        const LyricSyllable syllable{s.value(QStringLiteral("Text")).toString().trimmed(),
                                     ms(s.value(QStringLiteral("StartTime"))), ms(s.value(QStringLiteral("EndTime")))};
        if (!open) {
            word = LyricWord{QString(), syllable.startMs, syllable.endMs, {}};
            open = true;
        }
        word.text += syllable.text;
        word.endMs = syllable.endMs;
        word.syllables.append(syllable);
        if (!s.value(QStringLiteral("IsPartOfWord")).toBool()) {
            if (!word.text.isEmpty())
                line.words.append(word);
            open = false;
        }
    }
    if (open && !word.text.isEmpty())
        line.words.append(word);
    QStringList texts;
    for (const LyricWord &w : std::as_const(line.words))
        texts << w.text;
    line.text = texts.join(u' ');
    if (!line.words.isEmpty()) {
        line.startMs = std::min(line.startMs ? line.startMs : line.words.first().startMs, line.words.first().startMs);
        line.endMs = std::max(line.endMs, line.words.last().endMs);
    }
    return line;
}

} // namespace

SpicyLyricsProvider::SpicyLyricsProvider(const QUrl &baseUrl, KeySource key, QObject *parent)
    : LyricProvider(parent), m_baseUrl(baseUrl), m_key(std::move(key))
{
}

QString SpicyLyricsProvider::spotifyTrackId(const QString &mprisTrackId, const QUrl &url)
{
    static const QRegularExpression id(QStringLiteral(R"((?:/com/spotify/track/|spotify:track:|open\.spotify\.com/track/)([A-Za-z0-9]{22})(?![A-Za-z0-9]))"));
    for (const QString &candidate : {mprisTrackId, url.toString()})
        if (const auto m = id.match(candidate); m.hasMatch())
            return m.captured(1);
    return {};
}

void SpicyLyricsProvider::deliverLater(RequestId id, const LyricResult &r)
{
    QTimer::singleShot(0, this, [this, id, r] {
        const Pending pending = m_pending.take(id); // empty if cancelled
        if (pending.completion)
            pending.completion(r);
    });
}

RequestId SpicyLyricsProvider::fetch(const LyricQuery &query, Completion completion)
{
    const RequestId id = ++m_next;
    m_pending.insert(id, {nullptr, std::move(completion), query.spotifyTrackId, query.durationMs});
    static const QRegularExpression validId(QStringLiteral("^[A-Za-z0-9]{22}$"));
    const QString key = m_key ? m_key() : QString();
    if (key.isEmpty()) {
        deliverLater(id, result(LyricResult::Kind::NoMatch, QStringLiteral("no Spicy Lyrics API key")));
        return id;
    }
    if (!validId.match(query.spotifyTrackId).hasMatch()) {
        deliverLater(id, result(LyricResult::Kind::NoMatch, QStringLiteral("not a Spotify track")));
        return id;
    }
    if (const auto cached = m_cache.constFind(query.spotifyTrackId); cached != m_cache.cend()) {
        deliverLater(id, *cached);
        return id;
    }
    if (const qint64 now = Mpris::monotonicNowMs(); now < m_blockedUntilMs) {
        LyricResult r = result(LyricResult::Kind::ProviderError, QStringLiteral("Spicy Lyrics rate limit"));
        r.retryAfterSec = int((m_blockedUntilMs - now + 999) / 1000);
        deliverLater(id, r);
        return id;
    }

    QNetworkRequest request(m_baseUrl.resolved(QUrl(QStringLiteral("/v1/lyrics/") + query.spotifyTrackId)));
    request.setRawHeader("Authorization", "Bearer " + key.toUtf8());
    request.setHeader(QNetworkRequest::UserAgentHeader, LrcLibProvider::userAgent());
    request.setTransferTimeout(m_timeoutMs);
    QNetworkReply *reply = m_network.get(request);
    m_pending[id].reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, id] {
        Pending pending = m_pending.take(id);
        if (!pending.reply)
            return;
        QNetworkReply *reply = pending.reply;
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        LyricResult r;
        if (status == 429 || status == 503) {
            bool ok = false;
            int retryAfter = reply->rawHeader("Retry-After").trimmed().toInt(&ok);
            if (!ok || retryAfter <= 0)
                retryAfter = kDefaultRetryAfterSec;
            m_blockedUntilMs = Mpris::monotonicNowMs()
                               + qint64(retryAfter * 1000 * (1.0 + QRandomGenerator::global()->bounded(0.3)));
            r = result(LyricResult::Kind::ProviderError, QStringLiteral("Spicy Lyrics rate limit"));
            r.retryAfterSec = retryAfter;
        } else if (status == 404) {
            r = result(LyricResult::Kind::NoMatch, QStringLiteral("no Spicy Lyrics sync"));
        } else if (status == 401 || status == 403) {
            r = result(LyricResult::Kind::ProviderError, QStringLiteral("Spicy Lyrics rejected the API key"));
        } else if (status != 200) {
            r = result(LyricResult::Kind::ProviderError,
                       status ? QStringLiteral("Spicy Lyrics HTTP %1").arg(status) : reply->errorString());
        } else {
            const QJsonDocument json = QJsonDocument::fromJson(reply->readAll());
            r = json.isObject() ? parse(json.object().value(QStringLiteral("Body")).toObject(), pending.durationMs)
                                : result(LyricResult::Kind::InvalidResponse, QStringLiteral("response is not JSON"));
        }
        if (r.kind == LyricResult::Kind::Timed || r.kind == LyricResult::Kind::NoMatch)
            m_cache.insert(pending.trackId, r);
        if (pending.completion)
            pending.completion(r);
    });
    return id;
}

void SpicyLyricsProvider::cancel(RequestId id)
{
    const Pending pending = m_pending.take(id);
    if (pending.reply) {
        pending.reply->disconnect(this);
        pending.reply->abort();
        pending.reply->deleteLater();
    }
}

LyricResult SpicyLyricsProvider::parse(const QJsonObject &body, qint64 durationMs)
{
    const QString type = body.value(QStringLiteral("Type")).toString();
    LyricDocument doc;
    if (type == u"Syllable") {
        for (const QJsonValue &v : body.value(QStringLiteral("Content")).toArray()) {
            const QJsonObject item = v.toObject();
            LyricLine line = lineFromSyllables(item.value(QStringLiteral("Lead")).toObject());
            if (!line.words.isEmpty())
                doc.lines.append(line);
        }
    } else if (type == u"Line") {
        for (const QJsonValue &v : body.value(QStringLiteral("Content")).toArray()) {
            const QJsonObject item = v.toObject();
            const QJsonObject source = item.contains(QStringLiteral("Text")) ? item : item.value(QStringLiteral("Lead")).toObject();
            LyricLine line;
            line.text = source.value(QStringLiteral("Text")).toString().simplified();
            line.startMs = ms(source.value(QStringLiteral("StartTime")));
            line.endMs = ms(source.value(QStringLiteral("EndTime")));
            if (!line.text.isEmpty())
                doc.lines.append(line);
        }
    } else if (type == u"Static") {
        QStringList texts;
        for (const QJsonValue &v : body.value(QStringLiteral("Lines")).toArray())
            texts << v.toObject().value(QStringLiteral("Text")).toString();
        doc = PlainLyricsTimer::estimate(texts.join(u'\n'), durationMs);
    } else {
        return result(LyricResult::Kind::InvalidResponse, QStringLiteral("unknown Spicy Lyrics sync type"));
    }
    if (doc.lines.isEmpty())
        return result(LyricResult::Kind::NoMatch, QStringLiteral("empty Spicy Lyrics sync"));
    std::stable_sort(doc.lines.begin(), doc.lines.end(),
                     [](const LyricLine &a, const LyricLine &b) { return a.startMs < b.startMs; });

    // Attribution is required on screen: the provider always, and community credits.
    const QString source = body.value(QStringLiteral("source")).toString();
    doc.provider = providerName(source);
    if (source == u"spicy_lyrics") {
        const QJsonObject who = body.value(QStringLiteral("UploadAttribution")).toObject();
        for (const auto &[field, verb] : {std::pair{QStringLiteral("Maker"), QStringLiteral("synced by")},
                                          std::pair{QStringLiteral("Uploader"), QStringLiteral("uploaded by")}}) {
            const QJsonObject person = who.value(field).toObject();
            const QString name = person.value(QStringLiteral("username")).toString();
            if (!name.isEmpty())
                doc.credits.append({verb + u' ' + name, QUrl(person.value(QStringLiteral("url")).toString())});
        }
    }
    LyricResult r = result(LyricResult::Kind::Timed, {});
    r.document = std::move(doc);
    return r;
}
