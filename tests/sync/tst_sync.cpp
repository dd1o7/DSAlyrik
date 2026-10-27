#include <QSignalSpy>
#include <QtTest>

#include "sync/LyricSession.h"
#include "sync/LyricSyncEngine.h"

using namespace Qt::StringLiterals;
using Loc = LyricSyncEngine::Location;

namespace {

// Two lines with explicit words and a gap: [1000,2000) a b | gap | [3000,4000) c
LyricDocument sampleDoc()
{
    LyricDocument doc;
    doc.lines.append({u"a b"_s, {{u"a"_s, 1000, 1500}, {u"b"_s, 1500, 2000}}, 1000, 2000});
    doc.lines.append({u"c"_s, {{u"c"_s, 3000, 4000}}, 3000, 4000});
    return doc;
}

TrackSnapshot track(const QString &title, qint64 pos, PlaybackState state, const QString &id = u"1"_s)
{
    TrackSnapshot t;
    t.service = u"org.mpris.MediaPlayer2.test"_s;
    t.trackId = id;
    t.title = title;
    t.artist = u"Band"_s;
    t.lengthMs = 200'000;
    t.positionMs = pos;
    t.state = state;
    return t;
}

LyricResult timed(const QString &text)
{
    LyricResult r;
    r.kind = LyricResult::Kind::Timed;
    r.document.lines.append({text, {}, 0, 1000});
    return r;
}

class FakeProvider : public LyricProvider {
public:
    RequestId fetch(const LyricQuery &query, Completion completion) override
    {
        queries.append(query);
        completions.append(std::move(completion));
        return RequestId(queries.size());
    }
    void cancel(RequestId id) override { cancelled.append(id); }
    QList<LyricQuery> queries;
    QList<Completion> completions;
    QList<RequestId> cancelled;
};

} // namespace

class TestSync : public QObject {
    Q_OBJECT

private slots:
    // --- locate ---
    void locateBeforeFirstLine() { QCOMPARE(LyricSyncEngine::locate(sampleDoc(), 999), (Loc{-1, -1})); }
    void locateWordBoundaries()
    {
        const auto doc = sampleDoc();
        QCOMPARE(LyricSyncEngine::locate(doc, 1000), (Loc{0, 0}));
        QCOMPARE(LyricSyncEngine::locate(doc, 1499), (Loc{0, 0}));
        QCOMPARE(LyricSyncEngine::locate(doc, 1500), (Loc{0, 1}));
        QCOMPARE(LyricSyncEngine::locate(doc, 3500), (Loc{1, 0}));
    }
    void locateGapKeepsLineWithoutWord() { QCOMPARE(LyricSyncEngine::locate(sampleDoc(), 2500), (Loc{0, -1})); }
    void locateAfterLastLine() { QCOMPARE(LyricSyncEngine::locate(sampleDoc(), 9000), (Loc{1, -1})); }
    void locateEmptyDocument() { QCOMPARE(LyricSyncEngine::locate({}, 5000), (Loc{-1, -1})); }

    // --- engine clock and power ---
    void idleWithoutPlaybackOrLyrics()
    {
        qint64 now = 0;
        LyricSyncEngine engine(nullptr, [&] { return now; });
        engine.setTrack(track(u"x"_s, 0, PlaybackState::Playing));
        QVERIFY(!engine.isTicking()); // playing, but no lyrics
        engine.setLyrics(sampleDoc());
        QVERIFY(engine.isTicking());
        engine.setPlaybackState(PlaybackState::Paused);
        QVERIFY(!engine.isTicking());
        engine.setPlaybackState(PlaybackState::Playing);
        QVERIFY(engine.isTicking());
        engine.setPlaybackState(PlaybackState::Stopped);
        QVERIFY(!engine.isTicking());
        engine.setPlaybackState(PlaybackState::Playing);
        engine.setLyrics({});
        QVERIFY(!engine.isTicking());
    }

    void pauseFreezesPositionAndResumeContinues()
    {
        qint64 now = 0;
        LyricSyncEngine engine(nullptr, [&] { return now; });
        engine.setLyrics(sampleDoc());
        engine.setTrack(track(u"x"_s, 1000, PlaybackState::Playing));
        now = 600;
        QCOMPARE(engine.positionMs(), 1600);
        engine.setPlaybackState(PlaybackState::Paused);
        now = 5000;
        QCOMPARE(engine.positionMs(), 1600);
        engine.setPlaybackState(PlaybackState::Playing);
        now = 5400;
        QCOMPARE(engine.positionMs(), 2000);
    }

    void sleepsUntilNextBoundaryInsteadOfPolling()
    {
        qint64 now = 0;
        LyricSyncEngine engine(nullptr, [&] { return now; });
        engine.setLyrics(sampleDoc());
        engine.setTrack(track(u"x"_s, 1100, PlaybackState::Playing));
        QCOMPARE(engine.nextWakeMs(), 400); // next word starts at 1500
        engine.setTrack(track(u"x"_s, 2200, PlaybackState::Playing));
        QCOMPARE(engine.nextWakeMs(), 800); // gap until line 2 at 3000
        engine.setTrack(track(u"x"_s, 4500, PlaybackState::Playing));
        QVERIFY(!engine.isTicking()); // nothing left to reach
        engine.setContinuousFrames(true);
        QVERIFY(!engine.isTicking());
        engine.setTrack(track(u"x"_s, 1100, PlaybackState::Playing));
        QCOMPARE(engine.nextWakeMs(), LyricSyncEngine::kContinuousIntervalMs);
    }

    void suspendedEngineDoesNotWake()
    {
        qint64 now = 0;
        LyricSyncEngine engine(nullptr, [&] { return now; });
        engine.setLyrics(sampleDoc());
        engine.setTrack(track(u"x"_s, 1100, PlaybackState::Playing));
        QVERIFY(engine.isTicking());
        engine.setSuspended(true); // lyrics hidden: no timers
        QVERIFY(!engine.isTicking());
        engine.setTrack(track(u"x"_s, 1200, PlaybackState::Playing));
        QVERIFY(!engine.isTicking());
        QSignalSpy frames(&engine, &LyricSyncEngine::frameChanged);
        now = 2400; // time passes while hidden
        engine.setSuspended(false); // shown again: the current frame at once, timers back
        QVERIFY(engine.isTicking());
        QCOMPARE(frames.size(), 1);
        // Clock anchored at 1200 when now = 0, so now = 2400 means position 3600: line 1, word 0.
        QCOMPARE(frames.last().at(1).toInt(), 1);
        QCOMPARE(frames.last().at(2).toInt(), 0);
    }

    void leadShowsWordsEarly()
    {
        qint64 now = 0;
        LyricSyncEngine engine(nullptr, [&] { return now; });
        engine.setLeadMs(180);
        engine.setLyrics(sampleDoc());
        engine.setTrack(track(u"x"_s, 1330, PlaybackState::Playing));
        QCOMPARE(engine.location(), (Loc{0, 1})); // 1330 + 180 is past the 1500 boundary
        QCOMPARE(engine.positionMs(), 1330);       // playback position itself is unchanged
        QCOMPARE(engine.nextWakeMs(), 2000 - 1510);
    }

    void emitsOnlyOnWordChangesInRealTime()
    {
        LyricSyncEngine engine;
        LyricDocument doc;
        doc.lines.append({u"w1 w2 w3"_s, {{u"w1"_s, 0, 120}, {u"w2"_s, 120, 240}, {u"w3"_s, 240, 360}}, 0, 360});
        QSignalSpy frames(&engine, &LyricSyncEngine::frameChanged);
        engine.setLyrics(doc);
        engine.setTrack(track(u"x"_s, 0, PlaybackState::Playing));
        QTRY_COMPARE_WITH_TIMEOUT(engine.location(), (Loc{0, -1}), 1500); // past the end
        QVERIFY(!engine.isTicking());
        QList<int> words;
        for (const auto &f : frames)
            words.append(f.at(2).toInt());
        QVERIFY2(frames.size() <= 8, qPrintable(u"too many frames: %1"_s.arg(frames.size())));
        QVERIFY(words.contains(1) && words.contains(2));
    }

    void seekEmitsFrameImmediately()
    {
        qint64 now = 0;
        LyricSyncEngine engine(nullptr, [&] { return now; });
        engine.setLyrics(sampleDoc());
        engine.setTrack(track(u"x"_s, 1000, PlaybackState::Paused));
        QSignalSpy frames(&engine, &LyricSyncEngine::frameChanged);
        engine.setTrack(track(u"x"_s, 3500, PlaybackState::Paused));
        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames.last().at(1).toInt(), 1);
        QCOMPARE(frames.last().at(2).toInt(), 0);
        engine.setTrack(track(u"x"_s, 3520, PlaybackState::Paused)); // small resync: no redraw
        QCOMPARE(frames.size(), 1);
    }

    void synthesizesMissingWordTiming()
    {
        LyricSyncEngine engine;
        LyricDocument doc;
        doc.lines.append({u"two words"_s, {}, 0, 1000});
        engine.setLyrics(doc);
        QCOMPARE(engine.lyrics().lines[0].words.size(), 2);
    }

    // --- session ---
    void trackChangeFetchesAndClearsImmediately()
    {
        FakeProvider provider;
        LyricSyncEngine engine;
        LyricSession session(&provider, &engine);
        session.onActiveTrackChanged(track(u"First"_s, 0, PlaybackState::Playing));
        QCOMPARE(provider.queries.size(), 1);
        QCOMPARE(provider.queries[0].title, u"First"_s);
        QCOMPARE(provider.queries[0].durationMs, 200'000);
        QCOMPARE(session.status(), LyricSession::Status::Searching);

        provider.completions[0](timed(u"first lyric"_s));
        QCOMPARE(session.status(), LyricSession::Status::Ready);
        QCOMPARE(engine.lyrics().lines[0].text, u"first lyric"_s);

        session.onActiveTrackChanged(track(u"Second"_s, 0, PlaybackState::Playing, u"2"_s));
        QVERIFY(engine.lyrics().lines.isEmpty()); // cleared before the new lookup returns
        QCOMPARE(provider.queries.size(), 2);
    }

    void staleResponseCannotReplaceCurrentLyrics()
    {
        FakeProvider provider;
        LyricSyncEngine engine;
        LyricSession session(&provider, &engine);
        session.onActiveTrackChanged(track(u"Old"_s, 0, PlaybackState::Playing));
        session.onActiveTrackChanged(track(u"New"_s, 0, PlaybackState::Playing, u"2"_s));
        QCOMPARE(provider.cancelled, QList<RequestId>{1});

        provider.completions[1](timed(u"new lyric"_s));
        provider.completions[0](timed(u"old lyric"_s)); // a provider that ignored cancel
        QCOMPARE(engine.lyrics().lines[0].text, u"new lyric"_s);
        QCOMPARE(session.status(), LyricSession::Status::Ready);
    }

    void sameTrackUpdateDoesNotRefetch()
    {
        FakeProvider provider;
        LyricSyncEngine engine;
        LyricSession session(&provider, &engine);
        session.onActiveTrackChanged(track(u"Song"_s, 0, PlaybackState::Playing));
        session.onActiveTrackChanged(track(u"Song"_s, 50'000, PlaybackState::Paused));
        QCOMPARE(provider.queries.size(), 1);
        QVERIFY(!engine.isTicking());
    }

    void lateLengthRetriesAFailedLookup()
    {
        FakeProvider provider;
        LyricSyncEngine engine;
        LyricSession session(&provider, &engine);
        TrackSnapshot t = track(u"Song"_s, 0, PlaybackState::Playing);
        t.lengthMs = 0; // the player has not reported the length yet
        session.onActiveTrackChanged(t);
        QCOMPARE(provider.queries.size(), 1);
        QCOMPARE(provider.queries[0].durationMs, 0);
        provider.completions[0](LyricResult{LyricResult::Kind::NoMatch, {}, u"none"_s, 0});
        t.lengthMs = 200'000; // now it has
        session.onActiveTrackChanged(t);
        QCOMPARE(provider.queries.size(), 2);
        QCOMPARE(provider.queries[1].durationMs, 200'000);
        provider.completions[1](timed(u"found"_s));
        session.onActiveTrackChanged(t); // nothing new: no further lookups
        QCOMPARE(provider.queries.size(), 2);
    }

    void noMatchAndMissingTitle()
    {
        FakeProvider provider;
        LyricSyncEngine engine;
        LyricSession session(&provider, &engine);
        session.onActiveTrackChanged(track(u"Song"_s, 0, PlaybackState::Playing));
        provider.completions[0](LyricResult{LyricResult::Kind::NoMatch, {}, u"none"_s, 0});
        QCOMPARE(session.status(), LyricSession::Status::NoLyrics);

        session.onActiveTrackChanged(track({}, 0, PlaybackState::Playing, u"2"_s));
        QCOMPARE(provider.queries.size(), 1);
        QCOMPARE(session.status(), LyricSession::Status::NoLyrics);
    }

    void retriesOnceAfterShortRetryAfter()
    {
        FakeProvider provider;
        LyricSyncEngine engine;
        LyricSession session(&provider, &engine);
        session.onActiveTrackChanged(track(u"Song"_s, 0, PlaybackState::Playing));
        provider.completions[0](LyricResult{LyricResult::Kind::ProviderError, {}, u"busy"_s, 1});
        QCOMPARE(session.status(), LyricSession::Status::Searching);
        QTRY_COMPARE_WITH_TIMEOUT(provider.queries.size(), 2, 3000);
        provider.completions[1](LyricResult{LyricResult::Kind::ProviderError, {}, u"busy"_s, 1});
        QCOMPARE(session.status(), LyricSession::Status::Unavailable);
        QTest::qWait(1500);
        QCOMPARE(provider.queries.size(), 2);
    }

    void longRetryAfterAndPlayerLossGiveUp()
    {
        FakeProvider provider;
        LyricSyncEngine engine;
        LyricSession session(&provider, &engine);
        session.onActiveTrackChanged(track(u"Song"_s, 0, PlaybackState::Playing));
        provider.completions[0](LyricResult{LyricResult::Kind::ProviderError, {}, u"limited"_s, 60});
        QCOMPARE(session.status(), LyricSession::Status::Unavailable);

        session.onActiveTrackChanged(track(u"Next"_s, 0, PlaybackState::Playing, u"2"_s));
        session.onActiveTrackChanged(TrackSnapshot{});
        QCOMPARE(session.status(), LyricSession::Status::NoPlayer);
        QVERIFY(provider.cancelled.contains(2));
        QVERIFY(engine.lyrics().lines.isEmpty());
        QVERIFY(!engine.isTicking());
    }
};

QTEST_GUILESS_MAIN(TestSync)
#include "tst_sync.moc"
