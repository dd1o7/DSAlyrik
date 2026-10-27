#include "platform/OverlayWindow.h"

#ifdef DSALYRIK_HAVE_LAYER_SHELL
#include <LayerShellQt/Window>
#endif
#include <QFontDatabase>
#include <QMutex>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QGuiApplication>
#include <QQuickItem>
#include <QScreen>

#include "render/WordGlyphResolver.h"

// Serves the current album art to QML as image://art/<serial>.
class ArtworkProvider : public QQuickImageProvider {
public:
    ArtworkProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}
    void set(const QImage &image)
    {
        QMutexLocker lock(&m_mutex);
        m_image = image;
    }
    QImage requestImage(const QString &, QSize *size, const QSize &requested) override
    {
        QMutexLocker lock(&m_mutex);
        if (size)
            *size = m_image.size();
        if (m_image.isNull() || !requested.isValid() || requested.isEmpty())
            return m_image;
        return m_image.scaled(requested, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    }

private:
    QMutex m_mutex;
    QImage m_image;
};

void OverlayWindow::registerFonts()
{
    static const bool registered = [] {
        // Nunito (SIL OFL 1.1): the bundled default lyric face; see THIRD_PARTY_NOTICES.md.
        return QFontDatabase::addApplicationFont(QStringLiteral(":/dsalyrik/fonts/Nunito-Variable.ttf")) >= 0;
    }();
    Q_UNUSED(registered);
}

OverlayWindow::OverlayWindow(QObject *parent) : QObject(parent)
{
    registerFonts();
    m_view.setFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowTransparentForInput
                    | Qt::WindowDoesNotAcceptFocus | Qt::Tool);
    QSurfaceFormat format = m_view.format();
    format.setAlphaBufferSize(8); // X11 needs an ARGB visual for a see-through window
    m_view.setFormat(format);
    m_view.setColor(Qt::transparent);
    m_view.setTitle(QStringLiteral("DSAlyrik lyrics"));
    m_view.setResizeMode(QQuickView::SizeRootObjectToView);
#ifdef DSALYRIK_HAVE_LAYER_SHELL
    if (QGuiApplication::platformName().startsWith(u"wayland")) {
        // A layer-shell surface on the overlay layer: above windows, never focused,
        // ignoring panels' exclusive zones. Must be configured before the window exists.
        auto *layer = LayerShellQt::Window::get(&m_view);
        layer->setLayer(LayerShellQt::Window::LayerOverlay);
        layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        layer->setExclusiveZone(-1);
        layer->setActivateOnShow(false);
        layer->setScope(QStringLiteral("dsalyrik-lyrics"));
        layer->setAnchors(LayerShellQt::Window::Anchors()); // none: centred by the compositor
        m_layerShell = true;
    }
#endif
    m_artwork = new ArtworkProvider;
    m_view.engine()->addImageProvider(QStringLiteral("art"), m_artwork); // engine takes ownership
    m_view.setSource(QUrl(QStringLiteral("qrc:/dsalyrik/LyricOverlay.qml")));
    placeOnScreen();
    if (QQuickItem *root = m_view.rootObject())
        root->setProperty("shaderLens", usesShaderLens());
}

bool OverlayWindow::usesShaderLens() const
{
    // QT_QUICK_BACKEND=software selects the software scene graph without changing graphicsApi().
    return QQuickWindow::graphicsApi() != QSGRendererInterface::Software
           && QQuickWindow::sceneGraphBackend() != QLatin1String("software");
}

void OverlayWindow::setStyle(Style style)
{
    if (QQuickItem *root = m_view.rootObject())
        root->setProperty("style", int(style));
}

void OverlayWindow::setColors(const QColor &text, const QColor &highlight, const QColor &secondary)
{
    if (QQuickItem *root = m_view.rootObject()) {
        root->setProperty("textColor", text);
        root->setProperty("highlightColor", highlight);
        root->setProperty("secondaryColor", secondary);
    }
}

void OverlayWindow::setLyrics(const LyricDocument &document)
{
    m_document = document;
    m_lineStarts.clear();
    int next = 0;
    for (const LyricLine &line : document.lines) {
        m_lineStarts.append(next);
        next += int(line.words.size());
    }
    if (QQuickItem *root = m_view.rootObject()) {
        QStringList parts;
        if (!document.provider.isEmpty())
            parts << QStringLiteral("Lyrics: ") + document.provider;
        for (const LyricCredit &credit : document.credits)
            parts << credit.label;
        root->setProperty("attribution", parts.join(QStringLiteral(" · ")));
        root->setProperty("wordStream", toWordStream(document));
        setFrame(-1, -1);
    }
}

void OverlayWindow::setFrame(int lineIndex, int wordIndex, qint64 positionMs)
{
    QQuickItem *root = m_view.rootObject();
    if (!root)
        return;
    int global = -1;
    bool active = false;
    double progress = 1;
    qint64 remainingMs = 0;
    qint64 breakMs = 0; // > 0: instrumental break of this length; -1: open-ended (after the last line)
    if (lineIndex >= 0 && lineIndex < m_lineStarts.size()) {
        const LyricLine &line = m_document.lines[lineIndex];
        if (wordIndex >= 0 && wordIndex < line.words.size()) {
            const LyricWord &word = line.words[wordIndex];
            global = m_lineStarts[lineIndex] + wordIndex;
            active = true;
            const qint64 span = std::max<qint64>(1, word.endMs - word.startMs);
            progress = std::clamp(double(positionMs - word.startMs) / span, 0.0, 1.0);
            remainingMs = std::max<qint64>(0, word.endMs - positionMs);
        } else {
            // Between words or after the line: every word of the line has been sung.
            global = m_lineStarts[lineIndex] + int(line.words.size()) - 1;
            const bool last = lineIndex + 1 >= m_document.lines.size();
            const qint64 untilNext = last ? -1 : m_document.lines[lineIndex + 1].startMs - positionMs;
            if (last && positionMs >= line.endMs)
                breakMs = -1; // the outro: no later frame arrives, so show it now
            else if (untilNext > kBreakMinMs)
                breakMs = untilNext;
        }
    }
    QMetaObject::invokeMethod(root, "setFrame", Q_ARG(QVariant, global), Q_ARG(QVariant, active),
                              Q_ARG(QVariant, progress), Q_ARG(QVariant, int(remainingMs)),
                              Q_ARG(QVariant, int(breakMs)));
}

void OverlayWindow::setBackdrop(bool enabled)
{
    if (QQuickItem *root = m_view.rootObject())
        root->setProperty("backdrop", enabled);
}

void OverlayWindow::setTrackInfo(const QString &title, const QString &artist)
{
    if (QQuickItem *root = m_view.rootObject()) {
        root->setProperty("trackTitle", title);
        root->setProperty("trackArtist", artist);
    }
}

void OverlayWindow::setArtwork(const QImage &image)
{
    m_artwork->set(image);
    if (QQuickItem *root = m_view.rootObject())
        root->setProperty("artSource", image.isNull() ? QString()
                                                      : QStringLiteral("image://art/%1").arg(++m_artworkSerial));
}

void OverlayWindow::setImmersive(bool immersive)
{
    if (immersive == m_immersive)
        return;
    m_immersive = immersive;
    if (QQuickItem *root = m_view.rootObject())
        root->setProperty("immersive", immersive);
    placeOnScreen();
}

void OverlayWindow::setContinuousDrift(bool enabled)
{
    if (QQuickItem *root = m_view.rootObject())
        root->setProperty("continuousDrift", enabled);
}

void OverlayWindow::setArtBackdrop(bool enabled)
{
    if (QQuickItem *root = m_view.rootObject())
        root->setProperty("artBackdrop", enabled);
}

void OverlayWindow::setPlaying(bool playing)
{
    if (QQuickItem *root = m_view.rootObject())
        root->setProperty("playing", playing);
}

void OverlayWindow::setTimingNote(const QString &note)
{
    if (QQuickItem *root = m_view.rootObject())
        root->setProperty("timingNote", note);
}

void OverlayWindow::setStatusText(const QString &text)
{
    if (QQuickItem *root = m_view.rootObject())
        root->setProperty("statusText", text);
}

void OverlayWindow::setScreenName(const QString &name)
{
    QScreen *target = QGuiApplication::primaryScreen();
    for (QScreen *screen : QGuiApplication::screens())
        if (!name.isEmpty() && screen->name() == name)
            target = screen;
    if (target)
        m_view.setScreen(target);
    placeOnScreen();
}

void OverlayWindow::setTextAppearance(const QString &fontFamily, double scale)
{
    if (QQuickItem *root = m_view.rootObject()) {
        root->setProperty("fontFamily", fontFamily);
        root->setProperty("textScale", scale);
    }
}

void OverlayWindow::show()
{
    m_view.show();
}

void OverlayWindow::hide() { m_view.hide(); }

QString OverlayWindow::placementNote()
{
#ifdef DSALYRIK_HAVE_LAYER_SHELL
    if (QGuiApplication::platformName().startsWith(u"wayland"))
        return {}; // layer-shell surfaces float and click through natively
#endif
    if (QGuiApplication::platformName().startsWith(u"wayland"))
        return tr("On Wayland the compositor decides where the lyrics window appears and whether it stays "
                  "on top. On KDE Plasma, a KWin window rule for \"DSAlyrik lyrics\" can pin its position "
                  "and keep it above other windows; or start DSAlyrik with QT_QPA_PLATFORM=xcb to let it "
                  "place itself through XWayland.");
    return {};
}

QVariantList OverlayWindow::toQml(const LyricDocument &document)
{
    QVariantList lines;
    lines.reserve(document.lines.size());
    for (const LyricLine &line : document.lines) {
        QVariantList words;
        for (const LyricWord &word : line.words)
            words.append(QVariantMap{{QStringLiteral("text"), word.text},
                                     {QStringLiteral("glyph"), WordGlyphResolver::glyphFor(word.text)}});
        lines.append(QVariantMap{{QStringLiteral("text"), line.text}, {QStringLiteral("words"), words}});
    }
    return lines;
}

QVariantList OverlayWindow::toWordStream(const LyricDocument &document)
{
    QVariantList words;
    for (qsizetype lineIndex = 0; lineIndex < document.lines.size(); ++lineIndex) {
        const LyricLine &line = document.lines[lineIndex];
        QStringList texts;
        QStringList glyphs;
        for (const LyricWord &word : line.words) {
            texts << word.text;
            glyphs << WordGlyphResolver::glyphFor(word.text);
            // A phrase ending here ("broken heart") carries one symbol on its last word.
            int length = 0;
            const QString phrase = WordGlyphResolver::glyphForPhrase(texts.mid(std::max<qsizetype>(0, texts.size() - 3)), &length);
            if (!phrase.isEmpty()) {
                for (int k = 1; k < length; ++k)
                    glyphs[glyphs.size() - 1 - k].clear();
                glyphs.last() = phrase;
            }
        }
        for (qsizetype i = 0; i < texts.size(); ++i)
            words.append(QVariantMap{{QStringLiteral("text"), texts[i]},
                                     {QStringLiteral("glyph"), glyphs[i]},
                                     {QStringLiteral("line"), int(lineIndex)}});
    }
    return words;
}

QString OverlayWindow::styleName(Style style)
{
    switch (style) {
    case Style::Drift:
        return tr("Drift");
    case Style::Lens:
        return tr("Lens");
    case Style::LensGlyphs:
        return tr("Lens + Glyphs");
    case Style::Karaoke:
        return tr("Karaoke");
    case Style::Glyphs:
        return tr("Glyphs");
    }
    return {};
}

void OverlayWindow::placeOnScreen()
{
    // A large click-through canvas centred on the screen, like a lock-screen lyric wall.
    const QScreen *screen = m_view.screen() ? m_view.screen() : QGuiApplication::primaryScreen();
    if (!screen)
        return;
#ifdef DSALYRIK_HAVE_LAYER_SHELL
    if (m_layerShell) {
        // The compositor places layer surfaces: anchored to every edge when immersive,
        // otherwise centred at our requested size.
        using W = LayerShellQt::Window;
        W::Anchors fill;
        fill.setFlag(W::AnchorTop).setFlag(W::AnchorBottom).setFlag(W::AnchorLeft).setFlag(W::AnchorRight);
        W::get(&m_view)->setAnchors(m_immersive ? fill : W::Anchors());
    }
#endif
    if (m_immersive) {
        m_view.setGeometry(screen->geometry());
        return;
    }
    const QRect area = screen->availableGeometry();
    m_view.resize(int(area.width() * 0.72), int(area.height() * 0.78));
    m_view.setPosition(area.x() + (area.width() - m_view.width()) / 2,
                       area.y() + (area.height() - m_view.height()) / 2);
}
