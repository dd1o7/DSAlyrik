#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QtTest>
#include <optional>

#include "FakeHttpServer.h"
#include "lyrics/LrcLibProvider.h"
#include "lyrics/TrackNameNormalizer.h"

using namespace Qt::StringLiterals;

namespace {

QByteArray record(const QJsonObject &overrides = {})
{
    QJsonObject o{{"id", 1}, {"trackName", "Song"}, {"artistName", "Band"}, {"duration", 200},
                  {"instrumental", false}, {"plainLyrics", "one\ntwo"},
                  {"syncedLyrics", "[00:01.00]one\n[00:02.50]two"}};
    for (auto it = overrides.begin(); it != overrides.end(); ++it)
        o.insert(it.key(), it.value());
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

FakeHttpServer::Response ok(const QByteArray &body) { return {200, body, {}, false}; }

const LyricQuery kQuery{u"Song"_s, u"Band"_s, u"Album"_s, 200'400};

} // namespace

class TestLrcLib : public QObject {
    Q_OBJECT

    // Runs one fetch to completion and returns its result.
    static LyricResult run(LrcLibProvider &provider, const LyricQuery &query = kQuery)
    {
        std::optional<LyricResult> result;
        provider.fetch(query, [&](const LyricResult &r) { result = r; });
        if (!QTest::qWaitFor([&] { return result.has_value(); }, 5000))
            qFatal("fetch did not complete");
        return *result;
    }

private slots:
    void matchReturnsTimedLyricsAndSendsQuery()
    {
        FakeHttpServer server([](const auto &) { return ok(record()); });
        LrcLibProvider provider(server.url());
        const LyricResult r = run(provider);
        QCOMPARE(r.kind, LyricResult::Kind::Timed);
        QCOMPARE(r.document.lines.size(), 2);
        QCOMPARE(r.document.lines[1].startMs, 2500);
        QCOMPARE(r.document.title, u"Song"_s);

        QCOMPARE(server.requests.size(), 1);
        const QUrl target(QString::fromUtf8(server.requests[0].target));
        const QUrlQuery q(target);
        QCOMPARE(target.path(), u"/api/get"_s);
        QCOMPARE(q.queryItemValue(u"track_name"_s), u"Song"_s);
        QCOMPARE(q.queryItemValue(u"artist_name"_s), u"Band"_s);
        QCOMPARE(q.queryItemValue(u"album_name"_s), u"Album"_s);
        QCOMPARE(q.queryItemValue(u"duration"_s), u"200"_s);
        QVERIFY(server.requests[0].rawHead.contains("User-Agent: DSAlyrik/"));
    }

    void omitsUnknownDurationAndAlbum()
    {
        FakeHttpServer server([](const auto &) { return ok(record()); });
        LrcLibProvider provider(server.url());
        run(provider, {u"Song"_s, u"Band"_s, {}, 0});
        const QUrlQuery q(QUrl(QString::fromUtf8(server.requests[0].target)));
        QVERIFY(!q.hasQueryItem(u"duration"_s));
        QVERIFY(!q.hasQueryItem(u"album_name"_s));
    }

    void notFoundIsNoMatch()
    {
        FakeHttpServer server([](const auto &) {
            return FakeHttpServer::Response{404, R"({"name":"TrackNotFound","statusCode":404})", {}, false};
        });
        LrcLibProvider provider(server.url());
        QCOMPARE(run(provider).kind, LyricResult::Kind::NoMatch);
    }

    void instrumentalAndPlainOnlyAreNoMatch()
    {
        FakeHttpServer server([](const FakeHttpServer::Request &req) {
            if (req.target.contains("Instr"))
                return ok(record({{"instrumental", true}, {"syncedLyrics", QJsonValue::Null}}));
            return ok(record({{"syncedLyrics", QJsonValue::Null}}));
        });
        LrcLibProvider provider(server.url());
        QCOMPARE(run(provider, {u"Instr"_s, u"Band"_s, {}, 0}).kind, LyricResult::Kind::NoMatch);
        QCOMPARE(run(provider, {u"Plain"_s, u"Band"_s, {}, 0}).kind, LyricResult::Kind::NoMatch);
    }

    void plainOnlyRecordBecomesEstimatedTiming()
    {
        FakeHttpServer server([](const auto &) {
            return ok(record({{"syncedLyrics", QJsonValue::Null}, {"plainLyrics", "one line\nanother"}}));
        });
        LrcLibProvider provider(server.url());
        const LyricResult r = run(provider, {u"Song"_s, u"Band"_s, {}, 200'000});
        QCOMPARE(r.kind, LyricResult::Kind::Timed);
        QVERIFY(r.document.estimatedTiming);
        QCOMPARE(r.document.lines.size(), 2);
    }

    void httpErrorIsProviderError()
    {
        FakeHttpServer server([](const auto &) { return FakeHttpServer::Response{500, "{}", {}, false}; });
        LrcLibProvider provider(server.url());
        QCOMPARE(run(provider).kind, LyricResult::Kind::ProviderError);
        run(provider); // errors are not cached
        QCOMPARE(server.requests.size(), 2);
    }

    void rateLimitHonorsRetryAfter()
    {
        FakeHttpServer server([](const auto &) {
            return FakeHttpServer::Response{429, "edge said no", {{"Retry-After", "30"}}, false};
        });
        LrcLibProvider provider(server.url());
        const LyricResult first = run(provider);
        QCOMPARE(first.kind, LyricResult::Kind::ProviderError);
        QCOMPARE(first.retryAfterSec, 30);
        const LyricResult second = run(provider, {u"Other"_s, u"Band"_s, {}, 0});
        QCOMPARE(second.kind, LyricResult::Kind::ProviderError);
        QCOMPARE(server.requests.size(), 1); // blocked locally, no new request
    }

    void malformedResponsesAreInvalid()
    {
        FakeHttpServer server([](const FakeHttpServer::Request &req) {
            if (req.target.contains("Json"))
                return ok("{not json");
            if (req.target.contains("Array"))
                return ok("[]");
            return ok(record({{"syncedLyrics", "no timestamps at all"}}));
        });
        LrcLibProvider provider(server.url());
        QCOMPARE(run(provider, {u"Json"_s, u"B"_s, {}, 0}).kind, LyricResult::Kind::InvalidResponse);
        QCOMPARE(run(provider, {u"Array"_s, u"B"_s, {}, 0}).kind, LyricResult::Kind::InvalidResponse);
        QCOMPARE(run(provider, {u"Lrc"_s, u"B"_s, {}, 0}).kind, LyricResult::Kind::InvalidResponse);
    }

    void timeoutIsProviderError()
    {
        FakeHttpServer server([](const auto &) { return FakeHttpServer::Response{200, {}, {}, true}; });
        LrcLibProvider provider(server.url());
        provider.setTimeoutMs(300);
        QCOMPARE(run(provider).kind, LyricResult::Kind::ProviderError);
    }

    void cancelStopsRequestAndCompletion()
    {
        FakeHttpServer server([](const auto &) { return FakeHttpServer::Response{200, {}, {}, true}; });
        LrcLibProvider provider(server.url());
        bool called = false;
        const RequestId id = provider.fetch(kQuery, [&](const LyricResult &) { called = true; });
        QTRY_COMPARE(server.requests.size(), 1);
        provider.cancel(id);
        QTRY_COMPARE(server.abortedConnections, 1);
        QTest::qWait(100);
        QVERIFY(!called);
    }

    void emptyQueryNeverHitsNetwork()
    {
        FakeHttpServer server([](const auto &) { return ok(record()); });
        LrcLibProvider provider(server.url());
        bool syncCall = true;
        std::optional<LyricResult> result;
        provider.fetch({u"  "_s, u"Band"_s, {}, 0}, [&](const LyricResult &r) { result = r; });
        syncCall = result.has_value();
        QVERIFY(!syncCall); // completion is never synchronous
        QTRY_VERIFY(result.has_value());
        QCOMPARE(result->kind, LyricResult::Kind::NoMatch);
        QCOMPARE(run(provider, {u"Song"_s, {}, {}, 0}).kind, LyricResult::Kind::NoMatch);
        QCOMPARE(server.requests.size(), 0);
    }

    void normalizesTitlesAndArtists()
    {
        QCOMPARE(TrackNameNormalizer::cleanTitle(u"Song (feat. Someone)"_s), u"Song"_s);
        QCOMPARE(TrackNameNormalizer::cleanTitle(u"Song [Remastered 2011]"_s), u"Song"_s);
        QCOMPARE(TrackNameNormalizer::cleanTitle(u"Song - Remastered 2011"_s), u"Song"_s);
        QCOMPARE(TrackNameNormalizer::cleanTitle(u"Song - Radio Edit"_s), u"Song"_s);
        QCOMPARE(TrackNameNormalizer::cleanTitle(u"Theme (From \"Some Film\")"_s), u"Theme"_s);
        QCOMPARE(TrackNameNormalizer::cleanTitle(u"Song (Deluxe Edition) (with X)"_s), u"Song"_s);
        QCOMPARE(TrackNameNormalizer::cleanTitle(u"(I Can't Get No) Satisfaction"_s), u"(I Can't Get No) Satisfaction"_s);
        QCOMPARE(TrackNameNormalizer::cleanTitle(u"Anti-Hero"_s), u"Anti-Hero"_s);
        QCOMPARE(TrackNameNormalizer::primaryArtist(u"Alpha, Beta"_s), u"Alpha"_s);
        QCOMPARE(TrackNameNormalizer::primaryArtist(u"Alpha feat. Beta"_s), u"Alpha"_s);
        QCOMPARE(TrackNameNormalizer::primaryArtist(u"Simon & Garfunkel"_s), u"Simon & Garfunkel"_s);
        QCOMPARE(TrackNameNormalizer::comparable(u"  Café-Song!! "_s), TrackNameNormalizer::comparable(u"cafe song"_s));
    }

    void searchFallbackPicksBestCandidate()
    {
        FakeHttpServer server([](const FakeHttpServer::Request &req) {
            if (req.target.startsWith("/api/get"))
                return FakeHttpServer::Response{404, R"({"name":"TrackNotFound"})", {}, false};
            QJsonArray candidates{
                QJsonDocument::fromJson(record({{"id", 2}, {"duration", 260}})).object(), // wrong length
                QJsonDocument::fromJson(record({{"id", 3}, {"trackName", "Other"}})).object(), // wrong title
                QJsonDocument::fromJson(record({{"id", 4}, {"syncedLyrics", QJsonValue::Null}})).object(), // unsynced
                QJsonDocument::fromJson(record({{"id", 5}, {"duration", 201}, {"trackName", "Song (Remastered)"},
                                                {"syncedLyrics", "[00:03.00]picked"}})).object(),
            };
            return ok(QJsonDocument(candidates).toJson(QJsonDocument::Compact));
        });
        LrcLibProvider provider(server.url());
        const LyricResult r = run(provider, {u"Song - Remastered 2011"_s, u"Band, Guest"_s, {}, 200'400});
        QCOMPARE(r.kind, LyricResult::Kind::Timed);
        QCOMPARE(r.document.lines[0].text, u"picked"_s);
        QCOMPARE(server.requests.size(), 2);
        const QUrlQuery q(QUrl(QString::fromUtf8(server.requests[1].target)));
        QCOMPARE(QUrl(QString::fromUtf8(server.requests[1].target)).path(), u"/api/search"_s);
        QCOMPARE(q.queryItemValue(u"track_name"_s), u"Song"_s);
        QCOMPARE(q.queryItemValue(u"artist_name"_s), u"Band"_s);
    }

    void searchWithoutGoodCandidateIsNoMatch()
    {
        FakeHttpServer server([](const FakeHttpServer::Request &req) {
            if (req.target.startsWith("/api/get"))
                return FakeHttpServer::Response{404, "{}", {}, false};
            return ok("[" + record({{"duration", 300}}) + "]");
        });
        LrcLibProvider provider(server.url());
        QCOMPARE(run(provider).kind, LyricResult::Kind::NoMatch);
        QCOMPARE(server.requests.size(), 2);
    }

    void errorsDoNotTriggerSearch()
    {
        FakeHttpServer server([](const auto &) { return FakeHttpServer::Response{500, "{}", {}, false}; });
        LrcLibProvider provider(server.url());
        QCOMPARE(run(provider).kind, LyricResult::Kind::ProviderError);
        QCOMPARE(server.requests.size(), 1);
    }

    void cachesByNormalizedQuery()
    {
        FakeHttpServer server([](const auto &) { return ok(record()); });
        LrcLibProvider provider(server.url());
        run(provider, {u"Song"_s, u"Band"_s, {}, 200'000});
        const LyricResult cached = run(provider, {u"  song "_s, u"BAND"_s, {}, 200'300});
        QCOMPARE(cached.kind, LyricResult::Kind::Timed);
        QCOMPARE(server.requests.size(), 1);
        run(provider, {u"Song"_s, u"Band"_s, {}, 230'000}); // different duration is a different key
        QCOMPARE(server.requests.size(), 2);
    }
};

QTEST_GUILESS_MAIN(TestLrcLib)
#include "tst_lrclib.moc"
