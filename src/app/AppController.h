#pragma once

#include <QObject>
#include <memory>
#include <vector>

#include "lyrics/FallbackLyricProvider.h"
#include "lyrics/LocalLyricProvider.h"
#include "lyrics/LrcLibProvider.h"
#include "lyrics/SpicyLyricsProvider.h"
#include "platform/DisplaySleepInhibitor.h"
#include "platform/FullscreenWatcher.h"
#include "platform/IdleMonitor.h"
#include "platform/OverlayWindow.h"
#include "playback/MprisManager.h"
#include "render/PaletteExtractor.h"
#include "settings/AppSettings.h"
#include "sync/LyricSession.h"
#include "sync/LyricSyncEngine.h"
#include "ui/SettingsDialog.h"
#include "ui/TrayController.h"

// Owns and starts the long-lived services (playback, lyrics, sync, tray).
class AppController : public QObject {
    Q_OBJECT
public:
    explicit AppController(QObject *parent = nullptr);
    void start(bool forceOverlay = false);
    Q_INVOKABLE void showSettings();

    MprisManager &playback() { return m_playback; }
    LyricSyncEngine &sync() { return m_sync; }
    LyricSession &session() { return m_session; }
    AppSettings &settings() { return m_settings; }

    static QString statusText(LyricSession::Status status);
    static bool shouldKeepDisplayAwake(bool keepAwake, PlaybackState state, bool lyricsShown);
    static bool lyricsVisible(bool userVisible, int idleDelaySec, bool idle, PlaybackState state,
                              bool fullscreenInFront = false, bool fullscreenLyrics = false);
    Q_INVOKABLE void setFullscreenLyrics(bool on);

    // Draw lyrics slightly ahead of the reported playhead, so highlights land with the voice.
    static constexpr qint64 kLyricLeadMs = 180;

private:
    MprisManager m_playback;
    LrcLibProvider m_lyrics;
    LocalLyricProvider m_localLyrics;
    SpicyLyricsProvider m_spicyLyrics{QUrl(QStringLiteral("https://api.spicylyrics.org")),
                                      [this] { return m_settings.spicyLyricsKey(); }};
    // A sidecar .lrc first, then Spicy Lyrics (word-synced, Spotify, needs a key), then LRCLIB.
    FallbackLyricProvider m_lyricSources{{&m_localLyrics, &m_spicyLyrics, &m_lyrics}};
    LyricSyncEngine m_sync;
    LyricSession m_session;
    std::vector<std::unique_ptr<OverlayWindow>> m_overlays; // one per enabled screen
    bool m_overlaysShown = false;
    QString m_statusText;
    QColor m_colors[3];
    AppSettings m_settings;
    TrayController m_tray;
    PaletteExtractor m_palette;
    DisplaySleepInhibitor m_inhibitor;
    IdleMonitor m_idle;
    FullscreenWatcher m_fullscreen;
    std::unique_ptr<SettingsDialog> m_settingsDialog; // created on first use
    bool m_userWantsLyrics = true; // tray "Show lyrics" (or --show-overlay)
    bool m_fullscreenLyrics = false; // "Fullscreen lyrics" on demand
    QUrl m_artUrl;
    std::optional<Palette> m_artPalette; // album-art colors for the current track
    QString m_trackKey; // current song and player, for remembered timing
    QString m_playerId;
    QImage m_artImage;                   // current cover for the intro card and backdrop

    void applyColors();
    void rebuildOverlays();
    void configure(OverlayWindow &overlay) const;
    void setOverlaysShown(bool shown);
    void updateVisibility();
    void applyLead();
    template <typename F> void forEachOverlay(F f)
    {
        for (auto &overlay : m_overlays)
            f(*overlay);
    }
    void updateInhibition();
    void onTrackChanged(const TrackSnapshot &track);
    QString trayStatus(LyricSession::Status status) const;
};
