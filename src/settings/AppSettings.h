#pragma once

#include <QColor>
#include <QObject>
#include <QSettings>
#include <memory>

#include "platform/OverlayWindow.h"

// Persistent user settings. Values are validated on load; anything invalid
// falls back to its default. Setters persist immediately and emit only on change.
class AppSettings : public QObject {
    Q_OBJECT
public:
    // keyFile: where a Spicy Lyrics API key typed into Settings is kept (owner-only file,
    // never the settings file). Defaults to ~/.config/dsalyrik/spicylyrics.key.
    explicit AppSettings(std::unique_ptr<QSettings> store = nullptr, const QString &keyFile = {}, QObject *parent = nullptr);

    QString spicyLyricsKey() const; // $SPICY_LYRICS_SECRET_KEY, else the key file
    void setSpicyLyricsKey(const QString &key); // empty removes the file

    OverlayWindow::Style style() const { return m_style; }
    QColor textColor() const { return m_text; }
    QColor highlightColor() const { return m_highlight; }
    QColor secondaryColor() const { return m_secondary; }
    bool autoSync() const { return m_autoSync; }
    bool overlayVisible() const { return m_overlayVisible; }
    bool keepAwake() const { return m_keepAwake; } // display only, while playing
    bool backdrop() const { return m_backdrop; }   // soft dark glow behind the words
    bool artBackdrop() const { return m_artBackdrop; } // blurred album art behind the words
    bool continuousDrift() const { return m_continuousDrift; } // Drift style keeps moving while playing
    int idleDelaySec() const { return m_idleDelaySec; } // 0 = always; else show only after this much idle
    bool hideOverFullscreen() const { return m_hideOverFullscreen; }
    QStringList disabledDisplays() const { return m_disabledDisplays; } // screens without an overlay
    bool isDisplayEnabled(const QString &screenName) const { return !m_disabledDisplays.contains(screenName); }
    QString fontFamily() const { return m_fontFamily; } // empty = system font
    double textScale() const { return m_textScale; }
    int timingOffsetMs() const { return m_timingOffsetMs; } // + shows lyrics later, - earlier
    // Remembered nudges for one song (hashed title+artist) and one player; they add up.
    int trackOffsetMs(const QString &trackKey) const;
    int playerOffsetMs(const QString &playerId) const;
    int effectiveOffsetMs(const QString &trackKey, const QString &playerId) const;
    static QString trackKey(const QString &title, const QString &artist);
    static QString playerId(const QString &service);
    QString preferredPlayer() const { return m_preferredPlayer; } // empty = automatic

    void setStyle(OverlayWindow::Style style);
    void setColors(const QColor &text, const QColor &highlight, const QColor &secondary);
    void setAutoSync(bool enabled);
    void setOverlayVisible(bool visible);
    void setKeepAwake(bool enabled);
    void setBackdrop(bool enabled);
    void setArtBackdrop(bool enabled);
    void setContinuousDrift(bool enabled);
    void setIdleDelaySec(int seconds);
    void setHideOverFullscreen(bool enabled);
    void setDisplayEnabled(const QString &screenName, bool enabled);
    void setFontFamily(const QString &family);
    void setTextScale(double scale);
    void setTimingOffsetMs(int offsetMs);
    void setTrackOffsetMs(const QString &trackKey, int offsetMs);
    void setPlayerOffsetMs(const QString &playerId, int offsetMs);
    void setPreferredPlayer(const QString &service);

    static constexpr double kMinTextScale = 0.6;
    static constexpr double kMaxTextScale = 2.0;
    static constexpr int kMaxTimingOffsetMs = 2000;

    static const QColor kDefaultText;
    static const QColor kDefaultHighlight;
    static const QColor kDefaultSecondary;

signals:
    void styleChanged(OverlayWindow::Style style);
    void colorsChanged();
    void autoSyncChanged(bool enabled);
    void overlayVisibleChanged(bool visible);
    void keepAwakeChanged(bool enabled);
    void backdropChanged(bool enabled);
    void artBackdropChanged(bool enabled);
    void continuousDriftChanged(bool enabled);
    void idleDelayChanged(int seconds);
    void hideOverFullscreenChanged(bool enabled);
    void disabledDisplaysChanged();
    void textAppearanceChanged();
    void timingOffsetChanged(int offsetMs);
    void scopedTimingChanged();
    void preferredPlayerChanged(const QString &service);

private:
    void load();

    std::unique_ptr<QSettings> m_store;
    QString m_keyFile;
    OverlayWindow::Style m_style = OverlayWindow::Style::Glyphs;
    QColor m_text = kDefaultText;
    QColor m_highlight = kDefaultHighlight;
    QColor m_secondary = kDefaultSecondary;
    bool m_autoSync = true;
    bool m_overlayVisible = true;
    bool m_keepAwake = true;
    bool m_backdrop = true;
    bool m_artBackdrop = true;
    bool m_continuousDrift = false;
    int m_idleDelaySec = 0;
    bool m_hideOverFullscreen = true;
    QStringList m_disabledDisplays;
    QString m_fontFamily;
    double m_textScale = 1.0;
    int m_timingOffsetMs = 0;
    QString m_preferredPlayer;
};
