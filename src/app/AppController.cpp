#include "app/AppController.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QTextStream>

AppController::AppController(QObject *parent)
    : QObject(parent), m_session(&m_lyricSources, &m_sync), m_tray(&m_settings)
{
    connect(&m_playback, &MprisManager::activeTrackChanged, &m_session, &LyricSession::onActiveTrackChanged);
    connect(&m_sync, &LyricSyncEngine::lyricsChanged, this,
            [this] {
                const QString note = m_sync.lyrics().estimatedTiming ? tr("≈ estimated timing") : QString();
                if (m_settingsDialog)
                    m_settingsDialog->setLyricsCredits(m_sync.lyrics());
                forEachOverlay([this, note](OverlayWindow &o) {
                    o.setLyrics(m_sync.lyrics());
                    o.setTimingNote(note);
                });
            });
    connect(&m_sync, &LyricSyncEngine::frameChanged, this,
            [this](qint64 position, int line, int word) {
                forEachOverlay([=](OverlayWindow &o) { o.setFrame(line, word, position); });
            });
    connect(&m_session, &LyricSession::statusChanged, this,
            [this](LyricSession::Status status) {
                m_statusText = statusText(status);
                forEachOverlay([this](OverlayWindow &o) { o.setStatusText(m_statusText); });
                m_tray.setStatus(trayStatus(status));
            });

    // Settings drive the overlay, player choice and colors.
    connect(&m_settings, &AppSettings::styleChanged, this,
            [this](OverlayWindow::Style style) { forEachOverlay([=](OverlayWindow &o) { o.setStyle(style); }); });
    connect(&m_settings, &AppSettings::textAppearanceChanged, this, [this] {
        forEachOverlay([this](OverlayWindow &o) { o.setTextAppearance(m_settings.fontFamily(), m_settings.textScale()); });
    });
    connect(&m_settings, &AppSettings::timingOffsetChanged, this, &AppController::applyLead);
    connect(&m_settings, &AppSettings::scopedTimingChanged, this, &AppController::applyLead);
    connect(&m_settings, &AppSettings::continuousDriftChanged, this,
            [this](bool on) { forEachOverlay([on](OverlayWindow &o) { o.setContinuousDrift(on); }); });
    connect(&m_settings, &AppSettings::artBackdropChanged, this,
            [this](bool on) { forEachOverlay([on](OverlayWindow &o) { o.setArtBackdrop(on); }); });
    connect(&m_settings, &AppSettings::backdropChanged, this,
            [this](bool on) { forEachOverlay([on](OverlayWindow &o) { o.setBackdrop(on); }); });
    connect(&m_settings, &AppSettings::disabledDisplaysChanged, this, &AppController::rebuildOverlays);
    connect(qApp, &QGuiApplication::screenAdded, this, &AppController::rebuildOverlays);
    connect(qApp, &QGuiApplication::screenRemoved, this, &AppController::rebuildOverlays);
    connect(&m_settings, &AppSettings::colorsChanged, this, &AppController::applyColors);
    connect(&m_settings, &AppSettings::autoSyncChanged, this, &AppController::applyColors);
    connect(&m_settings, &AppSettings::overlayVisibleChanged, this, [this](bool visible) {
        m_userWantsLyrics = visible;
        updateVisibility();
    });
    connect(&m_settings, &AppSettings::idleDelayChanged, this, [this](int seconds) {
        m_idle.setTimeoutMs(seconds * 1000);
        updateVisibility();
    });
    connect(&m_idle, &IdleMonitor::idleChanged, this, &AppController::updateVisibility);
    connect(&m_fullscreen, &FullscreenWatcher::fullscreenChanged, this, &AppController::updateVisibility);
    connect(&m_settings, &AppSettings::hideOverFullscreenChanged, this, [this](bool on) {
        m_fullscreen.setEnabled(on);
        updateVisibility();
    });
    connect(&m_settings, &AppSettings::keepAwakeChanged, this, &AppController::updateInhibition);
    connect(&m_inhibitor, &DisplaySleepInhibitor::failed, this,
            [](const QString &message) { QTextStream(stderr) << message << Qt::endl; });
    connect(&m_settings, &AppSettings::preferredPlayerChanged, &m_playback, &MprisManager::setPreferredService);
    connect(&m_tray, &TrayController::quitRequested, qApp, &QCoreApplication::quit);
    connect(&m_tray, &TrayController::settingsRequested, this, &AppController::showSettings);
    connect(&m_tray, &TrayController::fullscreenLyricsToggled, this, &AppController::setFullscreenLyrics);

    connect(&m_playback, &MprisManager::playerListChanged, this,
            [this] { m_tray.setPlayers(m_playback.players(), m_playback.activeService()); });
    connect(&m_playback, &MprisManager::activeTrackChanged, this, &AppController::onTrackChanged);
    connect(&m_palette, &PaletteExtractor::paletteReady, this, [this](const QUrl &url, const Palette &palette) {
        if (url != m_artUrl)
            return;
        m_artPalette = palette;
        applyColors();
    });
    connect(&m_palette, &PaletteExtractor::artworkReady, this, [this](const QUrl &url, const QImage &image) {
        if (url != m_artUrl)
            return;
        m_artImage = image;
        forEachOverlay([this](OverlayWindow &o) { o.setArtwork(m_artImage); });
    });
}

void AppController::start(bool forceOverlay)
{
    applyLead();
    applyColors();
    rebuildOverlays();
    m_playback.setPreferredService(m_settings.preferredPlayer());
    m_playback.start();

    const bool trayShown = m_tray.show();
    if (!trayShown)
        QTextStream(stderr) << tr("No system tray is available, so DSAlyrik has no menu. The lyrics overlay "
                                  "stays visible; on GNOME, install an AppIndicator/StatusNotifierItem extension "
                                  "to get the menu. Quit with Ctrl+C or `pkill dsalyrik`.")
                            << Qt::endl;
    m_tray.setScreensaverAvailable(m_idle.isAvailable());
    m_idle.setTimeoutMs(m_settings.idleDelaySec() * 1000);
    m_fullscreen.setEnabled(m_settings.hideOverFullscreen());
    m_userWantsLyrics = forceOverlay || m_settings.overlayVisible() || !trayShown;
    updateVisibility();
}

void AppController::applyLead()
{
    // A positive user offset shows lyrics later, so it reduces the lead.
    m_sync.setLeadMs(kLyricLeadMs - m_settings.effectiveOffsetMs(m_trackKey, m_playerId));
}

void AppController::rebuildOverlays()
{
    m_overlays.clear();
    for (QScreen *screen : QGuiApplication::screens()) {
        if (!m_settings.isDisplayEnabled(screen->name()))
            continue;
        auto overlay = std::make_unique<OverlayWindow>();
        overlay->setScreenName(screen->name());
        configure(*overlay);
        if (m_overlaysShown)
            overlay->show();
        m_overlays.push_back(std::move(overlay));
    }
    setOverlaysShown(m_overlaysShown); // every display may now be off: re-apply suspension and keep-awake
}

void AppController::configure(OverlayWindow &overlay) const
{
    // A new window (screen plugged in, display re-enabled) starts from the current state.
    overlay.setStyle(m_settings.style());
    overlay.setBackdrop(m_settings.backdrop());
    overlay.setArtBackdrop(m_settings.artBackdrop());
    overlay.setContinuousDrift(m_settings.continuousDrift());
    overlay.setImmersive(m_fullscreenLyrics || (m_settings.idleDelaySec() > 0 && m_idle.isAvailable()));
    overlay.setTextAppearance(m_settings.fontFamily(), m_settings.textScale());
    overlay.setColors(m_colors[0], m_colors[1], m_colors[2]);
    overlay.setLyrics(m_sync.lyrics());
    overlay.setTimingNote(m_sync.lyrics().estimatedTiming ? tr("≈ estimated timing") : QString());
    overlay.setFrame(m_sync.location().line, m_sync.location().word, m_sync.displayPositionMs());
    overlay.setPlaying(m_playback.activeSnapshot().state == PlaybackState::Playing);
    overlay.setStatusText(m_statusText);
    const TrackSnapshot track = m_playback.activeSnapshot();
    overlay.setTrackInfo(track.title, track.artist);
    overlay.setArtwork(m_artImage);
}

void AppController::setOverlaysShown(bool shown)
{
    m_overlaysShown = shown;
    // Nothing on screen: no boundary timers and no position polling.
    m_sync.setSuspended(!shown || m_overlays.empty());
    m_playback.setResyncEnabled(shown && !m_overlays.empty());
    forEachOverlay([shown](OverlayWindow &o) { shown ? o.show() : o.hide(); });
    updateInhibition();
}

void AppController::applyColors()
{
    const bool useArt = m_settings.autoSync() && m_artPalette;
    m_colors[0] = useArt ? m_artPalette->text : m_settings.textColor();
    m_colors[1] = useArt ? m_artPalette->highlight : m_settings.highlightColor();
    m_colors[2] = useArt ? m_artPalette->secondary : m_settings.secondaryColor();
    forEachOverlay([this](OverlayWindow &o) { o.setColors(m_colors[0], m_colors[1], m_colors[2]); });
}

bool AppController::lyricsVisible(bool userVisible, int idleDelaySec, bool idle, PlaybackState state,
                                  bool fullscreenInFront, bool fullscreenLyrics)
{
    if (!userVisible)
        return false;
    if (fullscreenLyrics)
        return true; // asked for explicitly: no idle wait, and it wins over other fullscreen apps
    if (fullscreenInFront)
        return false;
    if (idleDelaySec <= 0)
        return true;
    return idle && state == PlaybackState::Playing; // screensaver mode
}

void AppController::updateVisibility()
{
    const bool screensaver = m_fullscreenLyrics || (m_settings.idleDelaySec() > 0 && m_idle.isAvailable());
    const bool shown = lyricsVisible(m_userWantsLyrics, screensaver ? m_settings.idleDelaySec() : 0, m_idle.isIdle(),
                                     m_playback.activeSnapshot().state,
                                     m_settings.hideOverFullscreen() && m_fullscreen.isFullscreenActive(),
                                     m_fullscreenLyrics);
    forEachOverlay([screensaver](OverlayWindow &o) { o.setImmersive(screensaver); });
    if (shown != m_overlaysShown)
        setOverlaysShown(shown);
}

bool AppController::shouldKeepDisplayAwake(bool keepAwake, PlaybackState state, bool lyricsShown)
{
    return keepAwake && state == PlaybackState::Playing && lyricsShown;
}

void AppController::updateInhibition()
{
    const bool lyricsShown = m_overlaysShown && !m_overlays.empty();
    m_inhibitor.setActive(shouldKeepDisplayAwake(m_settings.keepAwake(), m_playback.activeSnapshot().state, lyricsShown),
                          tr("Showing synchronized lyrics while music plays"));
}

void AppController::onTrackChanged(const TrackSnapshot &track)
{
    updateVisibility(); // screensaver mode follows play/pause
    updateInhibition();
    forEachOverlay([&](OverlayWindow &o) { o.setPlaying(track.state == PlaybackState::Playing); });
    m_tray.setPlayers(m_playback.players(), m_playback.activeService());
    m_tray.setStatus(trayStatus(m_session.status()));
    forEachOverlay([&](OverlayWindow &o) { o.setTrackInfo(track.title, track.artist); });
    const QString trackKey = AppSettings::trackKey(track.title, track.artist);
    const QString playerId = AppSettings::playerId(track.service);
    if (trackKey != m_trackKey || playerId != m_playerId) {
        m_trackKey = trackKey;
        m_playerId = playerId;
        applyLead();
    }
    m_tray.setTimingContext(m_trackKey, track.title, m_playerId, TrayController::playerLabel(track.service));
    if (m_settingsDialog) {
        m_settingsDialog->setTimingContext(m_trackKey, track.title, m_playerId, TrayController::playerLabel(track.service));
        m_settingsDialog->setPlayers(m_playback.players());
    }
    if (track.artUrl == m_artUrl)
        return;
    // New artwork: manual colors and no cover until the new art has loaded.
    m_artUrl = track.artUrl;
    m_artPalette.reset();
    m_artImage = {};
    forEachOverlay([](OverlayWindow &o) { o.setArtwork({}); });
    applyColors();
    if (m_artUrl.isValid() && !m_artUrl.isEmpty())
        m_palette.request(m_artUrl); // the cover feeds the intro card and backdrop; colors only with Auto Sync
    else
        m_palette.cancel();
}

void AppController::setFullscreenLyrics(bool on)
{
    m_fullscreenLyrics = on;
    m_tray.setFullscreenLyrics(on);
    updateVisibility();
}

void AppController::showSettings()
{
    if (!m_settingsDialog) {
        m_settingsDialog = std::make_unique<SettingsDialog>(&m_settings);
        const TrackSnapshot track = m_playback.activeSnapshot();
        m_settingsDialog->setTimingContext(m_trackKey, track.title, m_playerId, TrayController::playerLabel(track.service));
        m_settingsDialog->setPlayers(m_playback.players());
        m_settingsDialog->setLyricsCredits(m_sync.lyrics());
    }
    m_settingsDialog->show();
    m_settingsDialog->raise();
    m_settingsDialog->activateWindow();
}

QString AppController::trayStatus(LyricSession::Status status) const
{
    const TrackSnapshot track = m_playback.activeSnapshot();
    if (!track.isValid())
        return {};
    const QString playing = track.artist.isEmpty() ? track.title : tr("%1 — %2").arg(track.title, track.artist);
    const QString detail = statusText(status);
    return detail.isEmpty() ? playing : tr("%1 (%2)").arg(playing, detail);
}

QString AppController::statusText(LyricSession::Status status)
{
    switch (status) {
    case LyricSession::Status::Searching:
        return tr("Looking for lyrics…");
    case LyricSession::Status::NoLyrics:
        return tr("No synced lyrics for this track");
    case LyricSession::Status::Unavailable:
        return tr("Lyrics are unavailable right now");
    case LyricSession::Status::NoPlayer:
    case LyricSession::Status::Ready:
        break;
    }
    return {};
}

