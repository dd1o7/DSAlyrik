#include <QDBusObjectPath>
#include <QSignalSpy>
#include <QtTest>

#include "FakeMprisPlayer.h"
#include "playback/MprisManager.h"

class TestMpris : public QObject {
    Q_OBJECT

private slots:
    void clockHoldsWhilePaused()
    {
        PositionClock clock;
        clock.reset(5000, 100, false);
        QCOMPARE(clock.positionAt(10'000, 0), 5000);
    }

    void clockAdvancesWhilePlayingAndClamps()
    {
        PositionClock clock;
        clock.reset(1000, 100, true);
        QCOMPARE(clock.positionAt(600, 0), 1500);
        QCOMPARE(clock.positionAt(600, 1200), 1200);
        clock.reset(0, 0, true, 2.0);
        QCOMPARE(clock.positionAt(250, 0), 500);
    }

    void parsesFullMetadata()
    {
        TrackSnapshot s;
        Mpris::applyMetadata(s, {{QStringLiteral("mpris:trackid"), QVariant::fromValue(QDBusObjectPath("/t/1"))},
                                 {QStringLiteral("xesam:title"), QStringLiteral("Song")},
                                 {QStringLiteral("xesam:artist"), QStringList{QStringLiteral("A"), QStringLiteral("B")}},
                                 {QStringLiteral("xesam:album"), QStringLiteral("LP")},
                                 {QStringLiteral("mpris:length"), qlonglong(200'000'000)},
                                 {QStringLiteral("mpris:artUrl"), QStringLiteral("file:///tmp/a.png")}});
        QCOMPARE(s.trackId, QStringLiteral("/t/1"));
        QCOMPARE(s.title, QStringLiteral("Song"));
        QCOMPARE(s.artist, QStringLiteral("A, B"));
        QCOMPARE(s.album, QStringLiteral("LP"));
        QCOMPARE(s.lengthMs, 200'000);
        QCOMPARE(s.artUrl, QUrl("file:///tmp/a.png"));
    }

    void missingOptionalMetadataClearsFields()
    {
        TrackSnapshot s;
        s.artist = QStringLiteral("old");
        s.lengthMs = 9;
        s.artUrl = QUrl("file:///old");
        Mpris::applyMetadata(s, {{QStringLiteral("xesam:title"), QStringLiteral("Only title")}});
        QCOMPARE(s.title, QStringLiteral("Only title"));
        QVERIFY(s.artist.isEmpty());
        QCOMPARE(s.lengthMs, 0);
        QVERIFY(s.artUrl.isEmpty());
    }

    void discoversExistingAndNewPlayers()
    {
        FakeMprisPlayer early("early");
        early.setTrack(QStringLiteral("1"), QStringLiteral("Early Song"), {QStringLiteral("Early Artist")}, 180'000, QStringLiteral("file:///art.png"));
        early.setStatus("Playing", 2000);

        MprisManager manager;
        manager.start();
        QTRY_COMPARE(manager.activeService(), early.service());
        QTRY_COMPARE(manager.activeSnapshot().title, QStringLiteral("Early Song"));
        const auto snap = manager.activeSnapshot();
        QCOMPARE(snap.artist, QStringLiteral("Early Artist"));
        QCOMPARE(snap.lengthMs, 180'000);
        QCOMPARE(snap.state, PlaybackState::Playing);
        QCOMPARE(snap.artUrl, QUrl("file:///art.png"));
        QVERIFY(snap.positionMs >= 2000 && snap.positionMs < 10'000);

        FakeMprisPlayer late("late");
        QTRY_VERIFY(manager.players().contains(late.service()));
        QCOMPARE(manager.activeService(), early.service()); // late is not playing
    }

    void prefersMostRecentlyPlayingPlayer()
    {
        FakeMprisPlayer a("a"), b("b");
        a.setTrack(QStringLiteral("1"), QStringLiteral("A song"), {}, 0);
        b.setTrack(QStringLiteral("2"), QStringLiteral("B song"), {}, 0);
        MprisManager manager;
        manager.start();
        QTRY_COMPARE(manager.players().size(), 2);

        a.setStatus("Playing", 0);
        QTRY_COMPARE(manager.activeService(), a.service());
        QTest::qWait(20);
        b.setStatus("Playing", 0);
        QTRY_COMPARE(manager.activeService(), b.service());
        b.setStatus("Paused", 100);
        QTRY_COMPARE(manager.activeService(), a.service());

        manager.setPreferredService(b.service());
        QCOMPARE(manager.activeService(), b.service());
    }

    void handlesPlayerDisappearance()
    {
        auto player = std::make_unique<FakeMprisPlayer>("gone");
        player->setTrack(QStringLiteral("1"), QStringLiteral("Bye"), {}, 0);
        player->setStatus("Playing", 0);
        MprisManager manager;
        QSignalSpy listSpy(&manager, &MprisManager::playerListChanged);
        QSignalSpy trackSpy(&manager, &MprisManager::activeTrackChanged);
        manager.start();
        QTRY_COMPARE(manager.activeService(), player->service());

        player.reset();
        QTRY_VERIFY(manager.activeService().isEmpty());
        QVERIFY(manager.players().isEmpty());
        QVERIFY(!manager.activeSnapshot().isValid());
        QVERIFY(listSpy.count() >= 2);
        QVERIFY(!trackSpy.last().first().value<TrackSnapshot>().isValid());
    }

    void trackChangeAndSeekUpdateSnapshot()
    {
        FakeMprisPlayer player("seek");
        player.setTrack(QStringLiteral("1"), QStringLiteral("First"), {}, 300'000);
        player.setStatus("Paused", 1000);
        MprisManager manager;
        QSignalSpy trackSpy(&manager, &MprisManager::activeTrackChanged);
        manager.start();
        QTRY_COMPARE(manager.activeSnapshot().positionMs, 1000);

        player.seek(42'000);
        QTRY_COMPARE(manager.activeSnapshot().positionMs, 42'000);

        player.setTrack(QStringLiteral("2"), QStringLiteral("Second"), {}, 300'000);
        QTRY_COMPARE(manager.activeSnapshot().title, QStringLiteral("Second"));
        QCOMPARE(trackSpy.last().first().value<TrackSnapshot>().title, QStringLiteral("Second"));
    }

    void periodicResyncCorrectsSilentDrift()
    {
        FakeMprisPlayer player(QStringLiteral("drift"));
        player.setTrack(QStringLiteral("1"), QStringLiteral("Song"), {}, 300'000);
        player.setStatus(QStringLiteral("Playing"), 1000);
        MprisManager manager;
        manager.setResyncIntervalMs(100);
        manager.start();
        QTRY_COMPARE(manager.activeService(), player.service());
        QTRY_VERIFY(manager.isResyncing());
        player.setPositionSilently(20'000); // the player stalled or skipped without telling us
        QTRY_VERIFY(std::abs(manager.activeSnapshot().positionMs - 20'000) < 500);
    }

    void resyncCanBeSuspended()
    {
        FakeMprisPlayer player(QStringLiteral("hidden"));
        player.setTrack(QStringLiteral("1"), QStringLiteral("Song"), {}, 300'000);
        player.setStatus(QStringLiteral("Playing"), 1000);
        MprisManager manager;
        manager.setResyncIntervalMs(100);
        manager.start();
        QTRY_VERIFY(manager.isResyncing());
        manager.setResyncEnabled(false); // lyrics hidden
        QVERIFY(!manager.isResyncing());
        player.setStatus(QStringLiteral("Paused"), 1000);
        player.setStatus(QStringLiteral("Playing"), 1000);
        QTest::qWait(200);
        QVERIFY(!manager.isResyncing());
        manager.setResyncEnabled(true);
        QVERIFY(manager.isResyncing());
    }

    void noResyncWhilePausedOrIdle()
    {
        FakeMprisPlayer player(QStringLiteral("idle"));
        player.setTrack(QStringLiteral("1"), QStringLiteral("Song"), {}, 300'000);
        player.setStatus(QStringLiteral("Paused"), 1000);
        MprisManager manager;
        manager.setResyncIntervalMs(100);
        manager.start();
        QTRY_COMPARE(manager.activeService(), player.service());
        QTest::qWait(200);
        QVERIFY(!manager.isResyncing());
        player.setStatus(QStringLiteral("Playing"), 1000);
        QTRY_VERIFY(manager.isResyncing());
        player.setStatus(QStringLiteral("Stopped"), 0);
        QTRY_VERIFY(!manager.isResyncing());
    }

    void ignoresProxyPlayer()
    {
        FakeMprisPlayer proxy("playerctld");
        proxy.setStatus("Playing", 0);
        MprisManager manager;
        manager.start();
        QTest::qWait(200);
        QVERIFY(manager.players().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestMpris)
#include "tst_mpris.moc"
