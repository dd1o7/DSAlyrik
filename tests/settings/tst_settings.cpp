#include <QSignalSpy>
#include <QGuiApplication>
#include <QScreen>
#include <QTemporaryDir>
#include <QtTest>

#include "app/AppController.h"
#include "render/PaletteExtractor.h"
#include "settings/AppSettings.h"
#include "ui/TrayController.h"
#include "../lyrics/FakeHttpServer.h"

using namespace Qt::StringLiterals;
using Style = OverlayWindow::Style;

class TestSettings : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    QString iniPath() const { return m_dir.filePath(u"dsalyrik.ini"_s); }
    std::unique_ptr<QSettings> store() const { return std::make_unique<QSettings>(iniPath(), QSettings::IniFormat); }

    static QImage filled(const QColor &main, const QColor &other = {}, double otherShare = 0)
    {
        QImage img(64, 64, QImage::Format_RGB32);
        img.fill(main);
        if (other.isValid())
            for (int y = 0; y < int(64 * otherShare); ++y)
                for (int x = 0; x < 64; ++x)
                    img.setPixelColor(x, y, other);
        return img;
    }

private slots:
    void init() { QFile::remove(iniPath()); }

    // --- AppSettings ---
    void defaults()
    {
        AppSettings s(store());
        QCOMPARE(s.style(), Style::Glyphs);
        QCOMPARE(s.highlightColor(), AppSettings::kDefaultHighlight);
        QVERIFY(s.autoSync());
        QVERIFY(s.overlayVisible());
        QVERIFY(s.keepAwake());
        QVERIFY(s.backdrop());
        QVERIFY(s.artBackdrop());
        QVERIFY(!s.continuousDrift()); // costs frames: opt-in
        QCOMPARE(s.idleDelaySec(), 0);   // screensaver mode off: show while playing
        QVERIFY(s.hideOverFullscreen());
        QVERIFY(s.disabledDisplays().isEmpty());
        QVERIFY(s.fontFamily().isEmpty());
        QCOMPARE(s.textScale(), 1.0);
        QCOMPARE(s.timingOffsetMs(), 0);
        QVERIFY(s.preferredPlayer().isEmpty());
    }

    void persistsAcrossInstances()
    {
        {
            AppSettings s(store());
            s.setStyle(Style::LensGlyphs);
            s.setColors(Qt::red, Qt::green, Qt::blue);
            s.setAutoSync(false);
            s.setOverlayVisible(false);
            s.setKeepAwake(false);
            s.setBackdrop(false);
            s.setArtBackdrop(false);
            s.setContinuousDrift(true);
            s.setIdleDelaySec(120);
            s.setHideOverFullscreen(false);
            s.setDisplayEnabled(u"HDMI-1"_s, false);
            s.setFontFamily(u"Noto Serif"_s);
            s.setTextScale(1.4);
            s.setTimingOffsetMs(-200);
            s.setPreferredPlayer(u"org.mpris.MediaPlayer2.spotify"_s);
        }
        AppSettings s(store());
        QCOMPARE(s.style(), Style::LensGlyphs);
        QCOMPARE(s.textColor(), QColor(Qt::red));
        QCOMPARE(s.secondaryColor(), QColor(Qt::blue));
        QVERIFY(!s.autoSync());
        QVERIFY(!s.overlayVisible());
        QVERIFY(!s.keepAwake());
        QVERIFY(!s.backdrop());
        QVERIFY(!s.artBackdrop());
        QVERIFY(s.continuousDrift());
        QCOMPARE(s.idleDelaySec(), 120);
        QVERIFY(!s.hideOverFullscreen());
        QCOMPARE(s.disabledDisplays(), QStringList{u"HDMI-1"_s});
        QVERIFY(!s.isDisplayEnabled(u"HDMI-1"_s));
        QVERIFY(s.isDisplayEnabled(u"eDP-1"_s));
        QCOMPARE(s.fontFamily(), u"Noto Serif"_s);
        QCOMPARE(s.textScale(), 1.4);
        QCOMPARE(s.timingOffsetMs(), -200);
        QCOMPARE(s.preferredPlayer(), u"org.mpris.MediaPlayer2.spotify"_s);
    }

    void invalidStoredValuesFallBack()
    {
        {
            QSettings raw(iniPath(), QSettings::IniFormat);
            raw.setValue(u"overlay/style"_s, 9);
            raw.setValue(u"colors/text"_s, u"not-a-color"_s);
            raw.setValue(u"colors/highlight"_s, u"#12345"_s);
            raw.setValue(u"colors/secondary"_s, u"#336699"_s);
            raw.setValue(u"overlay/visible"_s, u"maybe"_s);
            raw.setValue(u"playback/player"_s, u"com.example.NotMpris"_s);
            raw.setValue(u"text/scale"_s, 9.0);
            raw.setValue(u"sync/offsetMs"_s, u"soon"_s);
        }
        AppSettings s(store());
        QCOMPARE(s.style(), Style::Glyphs);
        QCOMPARE(s.textColor(), AppSettings::kDefaultText);
        QCOMPARE(s.highlightColor(), AppSettings::kDefaultHighlight);
        QCOMPARE(s.secondaryColor(), QColor(u"#336699"_s));
        QVERIFY(s.overlayVisible());
        QVERIFY(s.preferredPlayer().isEmpty());
        QCOMPARE(s.textScale(), AppSettings::kMaxTextScale); // clamped, not rejected
        QCOMPARE(s.timingOffsetMs(), 0);
    }

    void rangesAreClamped()
    {
        AppSettings s(store());
        s.setTextScale(0.1);
        QCOMPARE(s.textScale(), AppSettings::kMinTextScale);
        s.setTimingOffsetMs(99'999);
        QCOMPARE(s.timingOffsetMs(), AppSettings::kMaxTimingOffsetMs);
        s.setDisplayEnabled(u"A"_s, false);
        s.setDisplayEnabled(u"A"_s, false);
        s.setDisplayEnabled(QString(), false);
        QCOMPARE(s.disabledDisplays(), QStringList{u"A"_s});
        s.setDisplayEnabled(u"A"_s, true);
        QVERIFY(s.disabledDisplays().isEmpty());
    }

    void perSongAndPerPlayerTiming()
    {
        const QString song = AppSettings::trackKey(u"Song - Remastered 2011"_s, u"Band, Guest"_s);
        QCOMPARE(song, AppSettings::trackKey(u"song"_s, u"BAND"_s)); // same song, same timing
        QVERIFY(!song.contains(u"song"_s, Qt::CaseInsensitive)); // hashed, not readable history
        QVERIFY(AppSettings::trackKey({}, u"Band"_s).isEmpty());
        QCOMPARE(AppSettings::playerId(u"org.mpris.MediaPlayer2.spotify"_s), u"spotify"_s);
        QCOMPARE(AppSettings::playerId(u"org.mpris.MediaPlayer2.firefox.instance_1_42"_s), u"firefox"_s);
        {
            AppSettings s(store());
            s.setTrackOffsetMs(song, -300);
            s.setPlayerOffsetMs(u"spotify"_s, 120);
            s.setTimingOffsetMs(50);
            QCOMPARE(s.effectiveOffsetMs(song, u"spotify"_s), -130);
            QCOMPARE(s.effectiveOffsetMs(u"other"_s, u"mpv"_s), 50);
            s.setTrackOffsetMs(u"k2"_s, 9999);
            QCOMPARE(s.trackOffsetMs(u"k2"_s), AppSettings::kMaxTimingOffsetMs);
        }
        AppSettings s(store());
        QCOMPARE(s.trackOffsetMs(song), -300);
        QCOMPARE(s.playerOffsetMs(u"spotify"_s), 120);
        QSignalSpy changed(&s, &AppSettings::scopedTimingChanged);
        s.setTrackOffsetMs(song, 0); // back to zero: the entry is removed
        QCOMPARE(changed.size(), 1);
        QVERIFY(!QSettings(iniPath(), QSettings::IniFormat).childGroups().contains(u"timing"_s)
                || !QSettings(iniPath(), QSettings::IniFormat).allKeys().join(u' ').contains(song));
    }

    void spicyLyricsKeyIsPrivate()
    {
        const QString keyFile = m_dir.filePath(u"spicylyrics.key"_s);
        AppSettings s(store(), keyFile);
        QVERIFY(s.spicyLyricsKey().isEmpty());
        s.setSpicyLyricsKey(u"  sl_sk_abc  "_s);
        QCOMPARE(s.spicyLyricsKey(), u"sl_sk_abc"_s);
        QCOMPARE(QFile(keyFile).permissions() & (QFile::ReadGroup | QFile::ReadOther | QFile::WriteGroup | QFile::WriteOther),
                 QFileDevice::Permissions()); // owner-only
        QVERIFY(!QSettings(iniPath(), QSettings::IniFormat).allKeys().join(u' ').contains(u"sl_sk"_s)); // never in the settings file
        qputenv("SPICY_LYRICS_SECRET_KEY", "sl_sk_env");
        QCOMPARE(s.spicyLyricsKey(), u"sl_sk_env"_s); // the environment wins
        qunsetenv("SPICY_LYRICS_SECRET_KEY");
        s.setSpicyLyricsKey({});
        QVERIFY(!QFile::exists(keyFile));
    }

    void emitsOnlyOnChange()
    {
        AppSettings s(store());
        QSignalSpy style(&s, &AppSettings::styleChanged);
        QSignalSpy colors(&s, &AppSettings::colorsChanged);
        s.setStyle(Style::Glyphs); // the default: no change
        s.setStyle(Style::Lens);
        s.setColors(s.textColor(), s.highlightColor(), s.secondaryColor());
        s.setColors(Qt::red, s.highlightColor(), s.secondaryColor());
        s.setColors(QColor(), Qt::red, Qt::red); // invalid color: ignored
        QCOMPARE(style.size(), 1);
        QCOMPARE(colors.size(), 1);
    }

    void screensaverVisibilityPolicy()
    {
        using P = PlaybackState;
        // Screensaver mode off: shown whenever the user wants lyrics.
        QVERIFY(AppController::lyricsVisible(true, 0, false, P::Paused));
        QVERIFY(!AppController::lyricsVisible(false, 0, true, P::Playing));
        // "Fullscreen lyrics" on demand: shown even when not idle (screensaver mode or not).
        QVERIFY(AppController::lyricsVisible(true, 60, false, P::Paused, false, true));
        QVERIFY(!AppController::lyricsVisible(false, 0, false, P::Playing, false, true)); // still respects "Show lyrics"
        // A fullscreen app in front always wins.
        QVERIFY(!AppController::lyricsVisible(true, 0, false, P::Playing, true));
        QVERIFY(!AppController::lyricsVisible(true, 60, true, P::Playing, true));
        // On: only while idle and playing.
        QVERIFY(AppController::lyricsVisible(true, 60, true, P::Playing));
        QVERIFY(!AppController::lyricsVisible(true, 60, false, P::Playing));
        QVERIFY(!AppController::lyricsVisible(true, 60, true, P::Paused));
        QVERIFY(!AppController::lyricsVisible(false, 60, true, P::Playing));
    }

    void keepAwakeOnlyWhileLyricsAreShownAndPlaying()
    {
        using P = PlaybackState;
        QVERIFY(AppController::shouldKeepDisplayAwake(true, P::Playing, true));
        QVERIFY(!AppController::shouldKeepDisplayAwake(true, P::Playing, false)); // lyrics hidden
        QVERIFY(!AppController::shouldKeepDisplayAwake(true, P::Paused, true));
        QVERIFY(!AppController::shouldKeepDisplayAwake(false, P::Playing, true));
    }

    // --- PaletteExtractor ---
    void paletteFollowsDominantHue()
    {
        const auto p = PaletteExtractor::fromImage(filled(QColor(200, 30, 30), QColor(20, 40, 200), 0.3));
        QVERIFY(p.has_value());
        const int hue = p->highlight.hsvHue();
        QVERIFY2(hue <= 15 || hue >= 345, qPrintable(QString::number(hue)));
        QVERIFY(p->highlight.valueF() >= 0.85); // bright enough to read
        QVERIFY(p->highlight.hsvSaturationF() <= 0.85); // restrained, not neon
        QVERIFY(p->secondary.hsvSaturationF() < p->highlight.hsvSaturationF());
        QVERIFY(p->text.valueF() > p->highlight.valueF()); // lyric is the light tint
        QVERIFY(p->secondary.valueF() < p->highlight.valueF()); // secondary is muted
    }

    void grayscaleOrEmptyArtHasNoPalette()
    {
        QVERIFY(!PaletteExtractor::fromImage(filled(QColor(128, 128, 128), Qt::white, 0.5)).has_value());
        QVERIFY(!PaletteExtractor::fromImage(QImage()).has_value());
    }

    void loadsLocalAndRemoteArt()
    {
        const QString path = m_dir.filePath(u"art.png"_s);
        QVERIFY(filled(QColor(30, 160, 60)).save(path));
        PaletteExtractor extractor;
        QSignalSpy ready(&extractor, &PaletteExtractor::paletteReady);
        QSignalSpy unavailable(&extractor, &PaletteExtractor::paletteUnavailable);

        QSignalSpy artwork(&extractor, &PaletteExtractor::artworkReady);
        extractor.request(QUrl::fromLocalFile(path));
        QTRY_COMPARE(ready.size(), 1);
        QCOMPARE(artwork.size(), 1); // the cover itself is handed over for the overlay
        QVERIFY(artwork[0][1].value<QImage>().width() <= PaletteExtractor::kMaxArtworkSide);
        const int hue = ready[0][1].value<Palette>().highlight.hsvHue();
        QVERIFY(hue > 90 && hue < 160);

        QByteArray png;
        {
            QBuffer buf(&png);
            buf.open(QIODevice::WriteOnly);
            filled(QColor(30, 60, 200)).save(&buf, "PNG");
        }
        FakeHttpServer server([&](const auto &) { return FakeHttpServer::Response{200, png, {}, false}; });
        extractor.request(server.url().resolved(QUrl(u"/cover.png"_s)));
        QTRY_COMPARE(ready.size(), 2);

        extractor.request(QUrl(u"data:image/png;base64,"_s + QString::fromLatin1(png.toBase64())));
        QTRY_COMPARE(ready.size(), 3); // inline art, as mpv and browsers report it

        extractor.request(QUrl::fromLocalFile(m_dir.filePath(u"missing.png"_s)));
        QTRY_COMPARE(unavailable.size(), 1);
        extractor.request(QUrl(u"ftp://example.invalid/a.png"_s));
        QTRY_COMPARE(unavailable.size(), 2);
    }

    void newRequestSupersedesPending()
    {
        FakeHttpServer server([](const auto &) { return FakeHttpServer::Response{200, {}, {}, true}; });
        const QString path = m_dir.filePath(u"art2.png"_s);
        QVERIFY(filled(QColor(200, 120, 20)).save(path));
        PaletteExtractor extractor;
        QSignalSpy ready(&extractor, &PaletteExtractor::paletteReady);
        QSignalSpy unavailable(&extractor, &PaletteExtractor::paletteUnavailable);
        extractor.request(server.url().resolved(QUrl(u"/slow.png"_s)));
        QTRY_COMPARE(server.requests.size(), 1);
        extractor.request(QUrl::fromLocalFile(path));
        QTRY_COMPARE(ready.size(), 1);
        QCOMPARE(ready[0][0].toUrl(), QUrl::fromLocalFile(path));
        QTest::qWait(100);
        QCOMPARE(unavailable.size(), 0); // the superseded request reports nothing
    }

    // --- TrayController ---
    void trayMenuWritesSettings()
    {
        AppSettings s(store());
        TrayController tray(&s);
        tray.setPlayers({u"org.mpris.MediaPlayer2.spotify"_s, u"org.mpris.MediaPlayer2.mpv"_s},
                        u"org.mpris.MediaPlayer2.mpv"_s);
        emit tray.menu()->aboutToShow();

        auto find = [&](const QString &text) -> QAction * {
            for (QAction *a : tray.menu()->findChildren<QAction *>())
                if (a->text().remove(u'&') == text)
                    return a;
            return nullptr;
        };
        QVERIFY(find(u"Lens + Glyphs"_s));
        find(u"Lens + Glyphs"_s)->trigger();
        QCOMPARE(s.style(), Style::LensGlyphs);
        QVERIFY(find(u"Lens + Glyphs"_s)->isChecked());

        find(u"Auto Sync colors from album art"_s)->trigger();
        QVERIFY(!s.autoSync());
        find(u"Show lyrics"_s)->trigger();
        QVERIFY(!s.overlayVisible());
        find(u"Keep display awake while playing"_s)->trigger();
        QVERIFY(!s.keepAwake());
        find(u"Hide over fullscreen apps"_s)->trigger();
        QVERIFY(!s.hideOverFullscreen());
        find(u"After 1 min idle"_s)->trigger();
        QCOMPARE(s.idleDelaySec(), 60);
        find(u"Off: show while playing"_s)->trigger();
        QCOMPARE(s.idleDelaySec(), 0);
        find(u"Keep Drift moving (uses more power)"_s)->trigger();
        QVERIFY(s.continuousDrift());
        find(u"Album art backdrop"_s)->trigger();
        QVERIFY(!s.artBackdrop());
        find(u"Soft backdrop"_s)->trigger();
        QVERIFY(!s.backdrop());

        find(u"Larger text"_s)->trigger();
        QCOMPARE(s.textScale(), 1.1);
        find(u"Reset text size"_s)->trigger();
        QCOMPARE(s.textScale(), 1.0);
        find(u"Show lyrics earlier"_s)->trigger();
        QCOMPARE(s.timingOffsetMs(), -100);
        find(u"Show lyrics later"_s)->trigger();
        find(u"Show lyrics later"_s)->trigger();
        QCOMPARE(s.timingOffsetMs(), 100);
        emit tray.menu()->aboutToShow(); // labels reflect the current offset
        QVERIFY(find(u"Reset timing (+100 ms)"_s));
        find(u"Reset timing (+100 ms)"_s)->trigger();
        QCOMPARE(s.timingOffsetMs(), 0);

        const QString screen = QGuiApplication::primaryScreen()->name();
        if (screen.isEmpty()) {
            // Unnamed screens (the offscreen test platform) cannot be remembered, so they are not toggleable.
            QVERIFY(find(u"Display 1"_s) && !find(u"Display 1"_s)->isEnabled());
        } else {
            find(screen)->trigger(); // displays are checkable on/off toggles
            QVERIFY(!s.isDisplayEnabled(screen));
        }

        QVERIFY(find(u"Mpv (playing)"_s)); // the active player is marked while selection is automatic
        find(u"Spotify"_s)->trigger();
        QCOMPARE(s.preferredPlayer(), u"org.mpris.MediaPlayer2.spotify"_s);
        find(u"Automatic"_s)->trigger();
        QVERIFY(s.preferredPlayer().isEmpty());

        tray.setTimingContext(u"k1"_s, u"Song"_s, u"spotify"_s, u"Spotify"_s);
        emit tray.menu()->aboutToShow();
        find(u"This song: earlier"_s)->trigger();
        QCOMPARE(s.trackOffsetMs(u"k1"_s), -100);
        find(u"Spotify: later"_s)->trigger();
        QCOMPARE(s.playerOffsetMs(u"spotify"_s), 100);
        emit tray.menu()->aboutToShow();
        QVERIFY(find(u"This song: reset (-100 ms)"_s));
        find(u"This song: reset (-100 ms)"_s)->trigger();
        QCOMPARE(s.trackOffsetMs(u"k1"_s), 0);
        tray.setTimingContext({}, {}, {}, {}); // nothing playing: song items are disabled
        emit tray.menu()->aboutToShow();
        QVERIFY(!find(u"This song: earlier"_s)->isEnabled());

        QSignalSpy fullscreenNow(&tray, &TrayController::fullscreenLyricsToggled);
        find(u"Fullscreen lyrics"_s)->trigger();
        QCOMPARE(fullscreenNow.size(), 1);
        QCOMPARE(fullscreenNow[0][0].toBool(), true);
        tray.setFullscreenLyrics(true);
        emit tray.menu()->aboutToShow();
        QVERIFY(find(u"Fullscreen lyrics"_s)->isChecked());

        QSignalSpy quit(&tray, &TrayController::quitRequested);
        find(u"Quit DSAlyrik"_s)->trigger();
        QCOMPARE(quit.size(), 1);
        QVERIFY(!TrayController::appIcon().isNull());
    }
};

QTEST_MAIN(TestSettings)
#include "tst_settings.moc"
