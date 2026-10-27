#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>
#include <optional>

#include "FakeHttpServer.h"
#include "lyrics/SpicyLyricsProvider.h"

using namespace Qt::StringLiterals;

namespace {

const QString kTrack = u"1QV6tiMFM6fSOKOGLMHYYg"_s;

QJsonObject syllable(const char *text, double start, double end, bool partOfWord)
{
    return {{"Text", text}, {"StartTime", start}, {"EndTime", end}, {"IsPartOfWord", partOfWord}};
}

QByteArray syllableBody(const QJsonObject &extra = {})
{
    QJsonObject body{{"Type", "Syllable"}, {"id", kTrack}, {"StartTime", 7.357}, {"EndTime", 234.7}, {"source", "apple_music"}};
    QJsonArray lead{syllable("Hel", 10.0, 10.3, true), syllable("lo", 10.3, 10.6, false),
                    syllable("bright", 10.6, 11.2, false), syllable("world", 11.4, 12.0, false)};
    body.insert("Content", QJsonArray{QJsonObject{
        {"Type", "Vocal"}, {"OppositeAligned", false},
        {"Lead", QJsonObject{{"Syllables", lead}, {"StartTime", 10.0}, {"EndTime", 12.0}}}}});
    for (auto it = extra.begin(); it != extra.end(); ++it)
        body.insert(it.key(), it.value());
    return QJsonDocument(QJsonObject{{"Body", body}, {"Status", 200}, {"Type", "object"}}).toJson(QJsonDocument::Compact);
}

FakeHttpServer::Response ok(const QByteArray &body) { return {200, body, {}, false}; }

LyricQuery query(const QString &id = kTrack)
{
    LyricQuery q{u"Song"_s, u"Band"_s, {}, 200'000};
    q.spotifyTrackId = id;
    return q;
}

} // namespace

class TestSpicyLyrics : public QObject {
    Q_OBJECT

    static LyricResult run(SpicyLyricsProvider &provider, const LyricQuery &q = query())
    {
        std::optional<LyricResult> result;
        provider.fetch(q, [&](const LyricResult &r) { result = r; });
        if (!QTest::qWaitFor([&] { return result.has_value(); }, 5000))
            qFatal("no result");
        return *result;
    }

private slots:
    void syllablesBecomeWordsWithRealTiming()
    {
        FakeHttpServer server([](const auto &) { return ok(syllableBody()); });
        SpicyLyricsProvider provider(server.url(), [] { return u"sl_sk_test"_s; });
        const LyricResult r = run(provider);
        QCOMPARE(r.kind, LyricResult::Kind::Timed);
        const LyricLine &line = r.document.lines[0];
        QCOMPARE(line.text, u"Hello bright world"_s);
        QCOMPARE(line.words.size(), 3);
        QCOMPARE(line.words[0].text, u"Hello"_s);
        QCOMPARE(line.words[0].startMs, 10'000);
        QCOMPARE(line.words[0].endMs, 10'600);
        QCOMPARE(line.words[0].syllables.size(), 2); // kept for karaoke
        QCOMPARE(line.words[2].startMs, 11'400);
        QCOMPARE(line.startMs, 10'000);
        QCOMPARE(line.endMs, 12'000);
        QCOMPARE(r.document.provider, u"Apple Music via Spicy Lyrics"_s);
        QVERIFY(r.document.credits.isEmpty()); // commercial source: provider only
        // Request shape: Spotify id in the path, secret key as a bearer token.
        QCOMPARE(QUrl(QString::fromUtf8(server.requests[0].target)).path(), u"/v1/lyrics/"_s + kTrack);
        QVERIFY(server.requests[0].rawHead.contains("Authorization: Bearer sl_sk_test"));
        QVERIFY(server.requests[0].rawHead.contains("User-Agent: DSAlyrik/"));
    }

    void communitySyncCreditsUploaderAndMaker()
    {
        const QJsonObject who{{"id", "1"}, {"username", "gc"}, {"url", "https://spicylyrics.org/uid/1"}};
        const QJsonObject up{{"id", "2"}, {"username", "spikerko"}, {"url", "https://spicylyrics.org/uid/2"}};
        FakeHttpServer server([&](const auto &) {
            return ok(syllableBody({{"source", "spicy_lyrics"}, {"UploadAttribution", QJsonObject{{"Maker", who}, {"Uploader", up}}}}));
        });
        SpicyLyricsProvider provider(server.url(), [] { return u"k"_s; });
        const LyricResult r = run(provider);
        QCOMPARE(r.document.provider, u"Spicy Lyrics community"_s);
        QCOMPARE(r.document.credits.size(), 2);
        QCOMPARE(r.document.credits[0].label, u"synced by gc"_s);
        QCOMPARE(r.document.credits[0].url, QUrl(u"https://spicylyrics.org/uid/1"_s));
        QCOMPARE(r.document.credits[1].label, u"uploaded by spikerko"_s);
    }

    void lineSyncedContent()
    {
        FakeHttpServer server([](const auto &) {
            QJsonObject body{{"Type", "Line"}, {"source", "spotify"},
                             {"Content", QJsonArray{QJsonObject{{"Type", "Vocal"}, {"Text", "first line"}, {"StartTime", 1.5}, {"EndTime", 4.0}},
                                                    QJsonObject{{"Type", "Vocal"}, {"Text", "second"}, {"StartTime", 4.0}, {"EndTime", 6.0}}}}};
            return ok(QJsonDocument(QJsonObject{{"Body", body}}).toJson());
        });
        SpicyLyricsProvider provider(server.url(), [] { return u"k"_s; });
        const LyricResult r = run(provider);
        QCOMPARE(r.kind, LyricResult::Kind::Timed);
        QCOMPARE(r.document.lines.size(), 2);
        QCOMPARE(r.document.lines[0].startMs, 1500);
        QVERIFY(r.document.lines[0].words.isEmpty()); // line-level: the synthesizer fills words
        QCOMPARE(r.document.provider, u"Spotify via Spicy Lyrics"_s);
    }

    void staticLyricsAreEstimated()
    {
        FakeHttpServer server([](const auto &) {
            QJsonObject body{{"Type", "Static"}, {"source", "spotify"},
                             {"Lines", QJsonArray{QJsonObject{{"Text", "one"}}, QJsonObject{{"Text", "two"}}}}};
            return ok(QJsonDocument(QJsonObject{{"Body", body}}).toJson());
        });
        SpicyLyricsProvider provider(server.url(), [] { return u"k"_s; });
        const LyricResult r = run(provider);
        QCOMPARE(r.kind, LyricResult::Kind::Timed);
        QVERIFY(r.document.estimatedTiming);
    }

    void needsKeyAndSpotifyIdBeforeAnyRequest()
    {
        FakeHttpServer server([](const auto &) { return ok(syllableBody()); });
        SpicyLyricsProvider noKey(server.url(), [] { return QString(); });
        QCOMPARE(run(noKey).kind, LyricResult::Kind::NoMatch);
        SpicyLyricsProvider withKey(server.url(), [] { return u"k"_s; });
        QCOMPARE(run(withKey, query({})).kind, LyricResult::Kind::NoMatch); // not a Spotify track
        QCOMPARE(run(withKey, query(u"not-an-id"_s)).kind, LyricResult::Kind::NoMatch);
        QCOMPARE(server.requests.size(), 0);
    }

    void errorsAndBackOff()
    {
        int status = 404;
        FakeHttpServer server([&](const auto &) {
            return FakeHttpServer::Response{status, "{}", {{"Retry-After", "20"}}, false};
        });
        SpicyLyricsProvider provider(server.url(), [] { return u"k"_s; });
        QCOMPARE(run(provider).kind, LyricResult::Kind::NoMatch);
        status = 401;
        QCOMPARE(run(provider, query(u"2WKGGQ6UmyUZGZgRJyEROB"_s)).kind, LyricResult::Kind::ProviderError);
        status = 429;
        const LyricResult limited = run(provider, query(u"7KNmIjcmGJIBrhP2s5Vioe"_s));
        QCOMPARE(limited.kind, LyricResult::Kind::ProviderError);
        QCOMPARE(limited.retryAfterSec, 20);
        const int before = int(server.requests.size());
        run(provider, query(u"3KNmIjcmGJIBrhP2s5Vioe"_s)); // blocked locally while backing off
        QCOMPARE(int(server.requests.size()), before);
    }

    void extractsSpotifyIds()
    {
        QCOMPARE(SpicyLyricsProvider::spotifyTrackId(u"/com/spotify/track/1QV6tiMFM6fSOKOGLMHYYg"_s, {}), kTrack);
        QCOMPARE(SpicyLyricsProvider::spotifyTrackId({}, QUrl(u"https://open.spotify.com/track/1QV6tiMFM6fSOKOGLMHYYg"_s)), kTrack);
        QCOMPARE(SpicyLyricsProvider::spotifyTrackId(u"spotify:track:1QV6tiMFM6fSOKOGLMHYYg"_s, {}), kTrack);
        QVERIFY(SpicyLyricsProvider::spotifyTrackId(u"/org/mpd/Tracks/12"_s, {}).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestSpicyLyrics)
#include "tst_spicylyrics.moc"
