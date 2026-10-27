#pragma once

#include <QColor>
#include <QObject>
#include <QQuickView>

#include "lyrics/LyricTypes.h"

// Frameless, transparent, click-through window hosting LyricOverlay.qml.
class OverlayWindow : public QObject {
    Q_OBJECT
public:
    enum class Style { Drift, Lens, LensGlyphs, Glyphs, Karaoke };
    Q_ENUM(Style)

    explicit OverlayWindow(QObject *parent = nullptr);

    QQuickView *view() { return &m_view; }
    void setStyle(Style style);
    void setColors(const QColor &text, const QColor &highlight, const QColor &secondary);
    void setLyrics(const LyricDocument &document);
    void setFrame(int lineIndex, int wordIndex, qint64 positionMs = 0);
    void setPlaying(bool playing);
    void setBackdrop(bool enabled);
    void setArtBackdrop(bool enabled);
    void setContinuousDrift(bool enabled);
    // Screensaver presentation: the whole screen, with a lock-screen-like dim.
    void setImmersive(bool immersive);
    void setTrackInfo(const QString &title, const QString &artist);
    void setArtwork(const QImage &image); // null = no artwork
    void setStatusText(const QString &text);
    void setTimingNote(const QString &note); // e.g. "≈ estimated timing"; empty hides it
    void setScreenName(const QString &name); // empty = primary screen
    void setTextAppearance(const QString &fontFamily, double scale);
    void show();
    void hide();

    bool usesShaderLens() const;
    bool usesLayerShell() const { return m_layerShell; } // native Wayland overlay surface
    // Empty when the platform honors placement and stacking requests.
    static QString placementNote();
    static constexpr qint64 kBreakMinMs = 4000; // shorter gaps are breaths, not breaks

    static QVariantList toQml(const LyricDocument &document);
    static QVariantList toWordStream(const LyricDocument &document);
    static void registerFonts();
    static QString styleName(Style style);

private:
    void placeOnScreen();

    QQuickView m_view;
    LyricDocument m_document;
    QList<int> m_lineStarts; // global index of each line's first word
    class ArtworkProvider *m_artwork = nullptr; // owned by the QML engine
    int m_artworkSerial = 0;
    bool m_immersive = false;
    bool m_layerShell = false;
};
