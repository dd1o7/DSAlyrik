#include <QApplication>
#include <QSocketNotifier>
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <QCommandLineParser>
#include <QPainter>
#include <QQuickView>
#include <QTime>
#include <QTimer>
#include <QTextStream>

#include "app/AppController.h"
#include "platform/FullscreenWatcher.h"
#include "platform/IdleMonitor.h"
#include "lyrics/LrcLibProvider.h"
#include "lyrics/LrcParser.h"
#include "render/PreviewLyrics.h"

using namespace Qt::StringLiterals;

namespace {

// Without LayerShellQt: Wayland gives ordinary windows no way to stay on top, and KWin ignores an
// empty input region on toplevels, so the overlay cannot float or click
// through there. Under XWayland, KWin honors keep-above and input shapes.
// Opt out with --native-wayland or by setting QT_QPA_PLATFORM yourself.
void preferXWaylandForOverlay(int argc, char *argv[])
{
    bool forceXWayland = false;
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--native-wayland") == 0)
            return;
        forceXWayland |= qstrcmp(argv[i], "--xwayland") == 0;
    }
#ifdef DSALYRIK_HAVE_LAYER_SHELL
    // Native layer-shell surfaces can float and click through on Wayland.
    if (!forceXWayland)
        return;
#endif
    if (qEnvironmentVariableIsSet("QT_QPA_PLATFORM") || !qEnvironmentVariableIsSet("DISPLAY"))
        return;
    if (qEnvironmentVariable("XDG_SESSION_TYPE") == u"wayland" || qEnvironmentVariableIsSet("WAYLAND_DISPLAY"))
        qputenv("QT_QPA_PLATFORM", "xcb");
}

int g_signalPipe[2] = {-1, -1};

// SIGTERM/SIGINT/SIGHUP become a normal Qt quit so destructors run and the
// display-idle inhibition is released (a killed process would leak it on KDE).
void quitOnTerminationSignals(QCoreApplication &app)
{
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, g_signalPipe) != 0)
        return;
    auto *notifier = new QSocketNotifier(g_signalPipe[1], QSocketNotifier::Read, &app);
    QObject::connect(notifier, &QSocketNotifier::activated, &app, [&app] {
        char byte;
        [[maybe_unused]] auto n = ::read(g_signalPipe[1], &byte, 1);
        app.quit();
    });
    struct sigaction action{};
    action.sa_handler = [](int) {
        char byte = 1;
        [[maybe_unused]] auto n = ::write(g_signalPipe[0], &byte, 1);
    };
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    for (int sig : {SIGTERM, SIGINT, SIGHUP})
        ::sigaction(sig, &action, nullptr);
}

} // namespace

int main(int argc, char *argv[])
{
    preferXWaylandForOverlay(argc, argv);
    QApplication app(argc, argv);
    quitOnTerminationSignals(app);
    QApplication::setApplicationName(QStringLiteral("dsalyrik"));
    QApplication::setApplicationDisplayName(QStringLiteral("DSAlyrik"));
    QApplication::setApplicationVersion(QStringLiteral(DSALYRIK_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("io.github.dd1o7.DSAlyrik"));
    QApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Synchronized lyrics for MPRIS players"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption showOverlay(QStringLiteral("show-overlay"),
                                   QStringLiteral("Show the lyrics overlay even if it was hidden from the tray menu."));
    QCommandLineOption preview(QStringLiteral("preview"),
                               QStringLiteral("Development: play synthetic lyrics in style <0-4>."), QStringLiteral("style"));
    QCommandLineOption screenshot(QStringLiteral("screenshot"),
                                  QStringLiteral("Development: with --preview, save the overlay on a dark backdrop to <file> and quit."),
                                  QStringLiteral("file"));
    QCommandLineOption previewAt(QStringLiteral("preview-at"),
                                 QStringLiteral("Development: start the preview at <ms> (default 4600)."), QStringLiteral("ms"),
                                 QStringLiteral("4600"));
    parser.addOption(QCommandLineOption(QStringLiteral("xwayland"),
                                        QStringLiteral("Run the overlay through XWayland instead of a native layer-shell surface.")));
    parser.addOption(QCommandLineOption(QStringLiteral("native-wayland"),
                                        QStringLiteral("Run as a native Wayland client (the overlay cannot stay on top or click through).")));
    parser.addOption(preview);
    parser.addOption(screenshot);
    parser.addOption(previewAt);
    QCommandLineOption previewPaused(QStringLiteral("preview-paused"),
                                     QStringLiteral("Development: keep the preview paused at --preview-at."));
    parser.addOption(previewPaused);
    QCommandLineOption fullscreenLyrics(QStringLiteral("fullscreen"), QStringLiteral("Start with fullscreen lyrics."));
    parser.addOption(fullscreenLyrics);
    QCommandLineOption watchIdle(QStringLiteral("watch-idle"),
                                 QStringLiteral("Development: print idle/active changes after <seconds> without input."),
                                 QStringLiteral("seconds"));
    parser.addOption(watchIdle);
    QCommandLineOption watchFullscreen(QStringLiteral("watch-fullscreen"),
                                       QStringLiteral("Development: print when a fullscreen window becomes active (KDE)."));
    parser.addOption(watchFullscreen);
    QCommandLineOption openSettings(QStringLiteral("settings"), QStringLiteral("Open the settings window at start."));
    parser.addOption(openSettings);
    parser.addOption(showOverlay);
    QCommandLineOption watchPlayback(QStringLiteral("watch-playback"),
                                     QStringLiteral("Development: print the active MPRIS track as it changes."));
    parser.addOption(watchPlayback);
    QCommandLineOption lookup(QStringLiteral("lookup"),
                              QStringLiteral("Development: fetch lyrics for <artist> <title> [seconds] and print timing only."));
    parser.addOption(lookup);
    QCommandLineOption watchLyrics(QStringLiteral("watch-lyrics"),
                                   QStringLiteral("Development: print lyric status and line/word changes (indexes only)."));
    parser.addOption(watchLyrics);
    parser.process(app);

    if (parser.isSet(lookup)) {
        const QStringList args = parser.positionalArguments();
        if (args.size() < 2)
            parser.showHelp(1);
        auto *provider = new LrcLibProvider(QUrl(QStringLiteral("https://lrclib.net")), &app);
        const LyricQuery query{args[1], args[0], {}, args.value(2).toLongLong() * 1000};
        provider->fetch(query, [](const LyricResult &r) {
            static const char *kinds[] = {"timed", "no-match", "provider-error", "invalid-response"};
            QTextStream out(stdout);
            out << kinds[int(r.kind)] << " | " << r.message << " | lines=" << r.document.lines.size();
            if (!r.document.lines.isEmpty())
                out << " | first=" << r.document.lines.first().startMs << "ms last=" << r.document.lines.last().startMs << "ms";
            out << Qt::endl;
            QCoreApplication::exit(r.kind == LyricResult::Kind::Timed ? 0 : 2);
        });
        return app.exec();
    }

    if (parser.isSet(watchFullscreen)) {
        auto *watcher = new FullscreenWatcher(&app);
        watcher->setEnabled(true);
        QTextStream(stdout) << "fullscreen watcher available=" << watcher->isAvailable() << Qt::endl;
        QObject::connect(watcher, &FullscreenWatcher::fullscreenChanged, [](bool on) {
            QTextStream(stdout) << QTime::currentTime().toString(u"hh:mm:ss"_s) << (on ? " fullscreen" : " normal") << Qt::endl;
        });
        const int rc = app.exec();
        delete watcher; // unloads the KWin script
        return rc;
    }

    if (parser.isSet(watchIdle)) {
        auto *idle = new IdleMonitor(&app);
        QTextStream(stdout) << "idle monitor available=" << idle->isAvailable() << Qt::endl;
        QObject::connect(idle, &IdleMonitor::idleChanged, [](bool on) {
            QTextStream(stdout) << QTime::currentTime().toString(u"hh:mm:ss"_s) << (on ? " idle" : " active") << Qt::endl;
        });
        idle->setTimeoutMs(parser.value(watchIdle).toInt() * 1000);
        const int rc = app.exec();
        delete idle;
        return rc;
    }

    if (parser.isSet(preview)) {
        auto *overlay = new OverlayWindow(&app);
        auto *engine = new LyricSyncEngine(&app);
        overlay->setStyle(OverlayWindow::Style(std::clamp(parser.value(preview).toInt(), 0, int(OverlayWindow::Style::Karaoke))));
        QObject::connect(engine, &LyricSyncEngine::frameChanged, overlay,
                         [overlay](qint64 position, int line, int word) { overlay->setFrame(line, word, position); });
        engine->setLyrics(std::get<LyricDocument>(LrcParser::parse(previewLrc())));
        overlay->setLyrics(engine->lyrics());
        overlay->setPlaying(!parser.isSet(previewPaused));
        overlay->setTrackInfo(QStringLiteral("Paper Boats"), QStringLiteral("The Lanterns"));
        overlay->setImmersive(parser.isSet(fullscreenLyrics));
        {
            // Synthetic cover: a warm diagonal gradient with a pale disc.
            QImage art(512, 512, QImage::Format_RGB32);
            QPainter p(&art);
            QLinearGradient g(0, 0, 512, 512);
            g.setColorAt(0, QColor(0xf2, 0x9b, 0x6c));
            g.setColorAt(1, QColor(0x5b, 0x1e, 0x4f));
            p.fillRect(art.rect(), g);
            p.setBrush(QColor(255, 240, 220, 200));
            p.setPen(Qt::NoPen);
            p.drawEllipse(QPoint(330, 190), 90, 90);
            p.end();
            overlay->setArtwork(art);
        }
        TrackSnapshot fake;
        fake.service = QStringLiteral("preview");
        fake.lengthMs = 14'000;
        fake.positionMs = parser.value(previewAt).toLongLong();
        fake.state = parser.isSet(previewPaused) ? PlaybackState::Paused : PlaybackState::Playing;
        engine->setTrack(fake);
        overlay->show();
        if (parser.isSet(screenshot)) {
            QTimer::singleShot(1200, engine, [engine] { engine->setPlaybackState(PlaybackState::Paused); });
            QTimer::singleShot(2500, &app, [overlay, path = parser.value(screenshot)] { // after transitions settle
                const QImage frame = overlay->view()->grabWindow();
                QImage out(frame.size(), QImage::Format_ARGB32);
                out.fill(QColor(0x1b, 0x1f, 0x2a));
                QPainter(&out).drawImage(0, 0, frame);
                const bool saved = out.save(path);
                QTextStream(stdout) << (saved ? "saved " : "FAILED ") << path << " shader=" << overlay->usesShaderLens()
                                    << " platform=" << QGuiApplication::platformName() << Qt::endl;
                QCoreApplication::exit(saved ? 0 : 1);
            });
        }
        const int rc = app.exec();
        // Destroy the QML scene while the application is still alive: tearing it down
        // during QApplication destruction leaves Qt's image loader thread waiting forever.
        delete overlay;
        delete engine;
        return rc;
    }

    AppController controller;
    if (parser.isSet(watchPlayback)) {
        QObject::connect(&controller.playback(), &MprisManager::activeTrackChanged, [](const TrackSnapshot &t) {
            static const char *states[] = {"stopped", "paused", "playing"};
            QTextStream(stdout) << (t.isValid() ? t.service : QStringLiteral("(no player)")) << " | "
                                << t.title << " | " << t.artist << " | " << states[int(t.state)] << " | "
                                << t.positionMs << "/" << t.lengthMs << " ms | " << t.artUrl.toString() << Qt::endl;
        });
    }
    if (parser.isSet(watchLyrics)) {
        QObject::connect(&controller.session(), &LyricSession::statusChanged,
                         [](LyricSession::Status status, const QString &message) {
                             QTextStream(stdout) << "status " << QVariant::fromValue(status).toString() << " "
                                                 << message << Qt::endl;
                         });
        QObject::connect(&controller.sync(), &LyricSyncEngine::frameChanged, [](qint64 pos, int line, int word) {
            QTextStream(stdout) << "frame " << pos << "ms line=" << line << " word=" << word << Qt::endl;
        });
    }
    controller.start(parser.isSet(showOverlay));
    if (parser.isSet(fullscreenLyrics))
        controller.setFullscreenLyrics(true);
    if (parser.isSet(openSettings))
        QMetaObject::invokeMethod(&controller, "showSettings", Qt::QueuedConnection);
    if (const QString note = OverlayWindow::placementNote(); !note.isEmpty())
        QTextStream(stderr) << note << Qt::endl;

    return app.exec();
}
