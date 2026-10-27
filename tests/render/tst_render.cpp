#include <QQuickItem>
#include <QRawFont>
#include <QtTest>

#include "lyrics/LrcParser.h"
#include "lyrics/WordTimingSynthesizer.h"
#include "platform/OverlayWindow.h"
#include "render/PreviewLyrics.h"
#include "render/WordGlyphResolver.h"

using namespace Qt::StringLiterals;
using Style = OverlayWindow::Style;

namespace {

// Synthetic lyrics written for tests; never real song text.
LyricDocument synthetic(bool longLine = false)
{
    QString lrc = previewLrc().section(u'\n', 0, 2) + u'\n';
    if (longLine)
        lrc += u"[00:10.00]" + u"wandering "_s.repeated(30).trimmed() + u"\n"_s;
    return WordTimingSynthesizer::expand(std::get<LyricDocument>(LrcParser::parse(lrc)));
}

// Fraction of pixels that are not fully transparent.
double coverage(const QImage &image)
{
    const QImage img = image.convertToFormat(QImage::Format_ARGB32);
    qint64 visible = 0;
    for (int y = 0; y < img.height(); ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x)
            visible += qAlpha(row[x]) > 0;
    }
    return double(visible) / (img.width() * img.height());
}

QImage render(OverlayWindow &overlay, QSize size)
{
    overlay.view()->resize(size);
    overlay.view()->show();
    if (!QTest::qWaitForWindowExposed(overlay.view()))
        qFatal("overlay not exposed");
    QTest::qWait(900); // let word transitions (entry + fade, ~600 ms) settle
    return overlay.view()->grabWindow();
}

} // namespace

class TestRender : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        // Any QML warning (binding loop, type error, missing file) fails the test.
        QTest::failOnWarning(QRegularExpression(u".*"_s));
    }

    void glyphsForCommonWords()
    {
        QVERIFY(!WordGlyphResolver::glyphFor(u"rain"_s).isEmpty());
        QCOMPARE(WordGlyphResolver::glyphFor(u"Rain,"_s), WordGlyphResolver::glyphFor(u"rain"_s));
        QCOMPARE(WordGlyphResolver::glyphFor(u"stars"_s), WordGlyphResolver::glyphFor(u"star"_s));
        QCOMPARE(WordGlyphResolver::glyphFor(u"heart's"_s), WordGlyphResolver::glyphFor(u"heart"_s));
        QVERIFY(WordGlyphResolver::glyphFor(u"the"_s).isEmpty());
        QVERIFY(WordGlyphResolver::glyphFor(u"..."_s).isEmpty());
    }

    void glyphTableIsLargeAndUnambiguous()
    {
        QVERIFY2(WordGlyphResolver::tableSize() >= 700, qPrintable(QString::number(WordGlyphResolver::tableSize())));
        QCOMPARE(WordGlyphResolver::duplicateWords(), QStringList());
    }

    void glyphsFollowInflections()
    {
        const auto same = [](const QString &form, const QString &base) {
            const QString a = WordGlyphResolver::glyphFor(form), b = WordGlyphResolver::glyphFor(base);
            if (a.isEmpty() || a != b)
                qWarning() << form << a << "vs" << base << b;
            return !a.isEmpty() && a == b;
        };
        QVERIFY(same(u"running"_s, u"run"_s));
        QVERIFY(same(u"cried"_s, u"cry"_s));
        QVERIFY(same(u"dancing"_s, u"dance"_s));
        QVERIFY(same(u"loved"_s, u"love"_s));
        QVERIFY(same(u"brighter"_s, u"bright"_s));
        QVERIFY(same(u"stopped"_s, u"stop"_s));
        QVERIFY(same(u"cherries"_s, u"cherry"_s));
        QVERIFY(same(u"Dreaming,"_s, u"dream"_s));
    }

    void contractionsDoNotMisfire()
    {
        QVERIFY(WordGlyphResolver::glyphFor(u"won't"_s).isEmpty()); // not "won" -> trophy
        QCOMPARE(WordGlyphResolver::glyphFor(u"you're"_s), WordGlyphResolver::glyphFor(u"you"_s));
        QCOMPARE(WordGlyphResolver::glyphFor(u"night’s"_s), WordGlyphResolver::glyphFor(u"night"_s));
        QVERIFY(WordGlyphResolver::glyphFor(u"I'll"_s).isEmpty());
    }

    void symbolFontCoversTheTable()
    {
        const QRawFont font = QRawFont::fromFont(QFont(u"Noto Emoji"_s));
        if (!font.isValid() || !font.familyName().contains(u"Noto Emoji"_s))
            QSKIP("monochrome Noto Emoji is not installed");
        QStringList missing;
        for (const QString &symbol : WordGlyphResolver::symbols())
            if (!font.supportsCharacter(symbol.toUcs4().value(0)))
                missing << symbol;
        QVERIFY2(missing.isEmpty(), qPrintable(missing.join(u' ')));
    }

    void phrasesCarryOneSymbol()
    {
        QCOMPARE(WordGlyphResolver::glyphForPhrase({u"my"_s, u"broken"_s, u"heart"_s}), WordGlyphResolver::glyphForPhrase({u"broken"_s, u"heart"_s}));
        QVERIFY(!WordGlyphResolver::glyphForPhrase({u"Broken,"_s, u"heart."_s}).isEmpty()); // punctuation and case ignored
        QVERIFY(WordGlyphResolver::glyphForPhrase({u"the"_s, u"boats"_s}).isEmpty());
        QVERIFY(WordGlyphResolver::phraseCount() >= 80);

        LyricDocument doc = WordTimingSynthesizer::expand(std::get<LyricDocument>(
            LrcParser::parse(u"[00:01.00]my broken heart again\n[00:05.00]a shooting star\n"_s)));
        const QVariantList words = OverlayWindow::toWordStream(doc);
        const auto glyph = [&](int i) { return words[i].toMap().value(u"glyph"_s).toString(); };
        QCOMPARE(glyph(2), WordGlyphResolver::glyphForPhrase({u"broken"_s, u"heart"_s})); // on "heart"
        QVERIFY(glyph(1).isEmpty());                                                      // not on "broken"
        QCOMPARE(glyph(6), WordGlyphResolver::glyphForPhrase({u"shooting"_s, u"star"_s}));
        QVERIFY(glyph(5).isEmpty());
    }

    void qmlModelCarriesWordsAndGlyphs()
    {
        const QVariantList lines = OverlayWindow::toQml(synthetic());
        QCOMPARE(lines.size(), 3);
        const QVariantList words = lines[0].toMap().value(u"words"_s).toList();
        QCOMPARE(words.size(), 7);
        QCOMPARE(words[6].toMap().value(u"text"_s).toString(), u"rain"_s);
        QVERIFY(!words[6].toMap().value(u"glyph"_s).toString().isEmpty());
    }

    void softwareBackendUsesLensFallback()
    {
        OverlayWindow overlay;
        render(overlay, {800, 240});
        QVERIFY(!overlay.usesShaderLens());
    }

    void stylesRenderLyrics_data()
    {
        QTest::addColumn<Style>("style");
        QTest::addColumn<QSize>("size");
        QTest::addColumn<bool>("longLine");
        for (Style s : {Style::Drift, Style::Lens, Style::LensGlyphs, Style::Glyphs, Style::Karaoke}) {
            const QByteArray name = OverlayWindow::styleName(s).toUtf8();
            QTest::newRow(name + " small") << s << QSize(480, 160) << false;
            QTest::newRow(name + " large") << s << QSize(1600, 320) << false;
            QTest::newRow(name + " long line") << s << QSize(800, 240) << true;
        }
    }

    void stylesRenderLyrics()
    {
        QFETCH(Style, style);
        QFETCH(QSize, size);
        QFETCH(bool, longLine);
        OverlayWindow overlay;
        overlay.setBackdrop(false); // measure the words themselves
        overlay.setStyle(style);
        overlay.setLyrics(synthetic(longLine));
        overlay.setFrame(longLine ? 3 : 0, longLine ? 12 : 6);
        const QImage image = render(overlay, size);
        QCOMPARE(image.size(), size);
        const double c = coverage(image);
        QVERIFY2(c > 0.005 && c < 0.6, qPrintable(u"coverage %1"_s.arg(c)));
    }

    void emptyStatesRender()
    {
        OverlayWindow overlay;
        overlay.setStatusText(u"No synced lyrics for this track"_s);
        QVERIFY(coverage(render(overlay, {800, 240})) > 0.002); // status message visible

        overlay.setStatusText({});
        QTest::qWait(900);
        QCOMPARE(coverage(overlay.view()->grabWindow()), 0.0); // nothing playing: fully clear

        overlay.setLyrics(synthetic());
        overlay.setFrame(-1, -1); // before the first word: nothing is shown yet
        QTest::qWait(900);
        QCOMPARE(coverage(overlay.view()->grabWindow()), 0.0);
    }

    void highlightBlockOnlyWhileSung()
    {
        auto highlightPixels = [](const QImage &image) {
            const QImage img = image.convertToFormat(QImage::Format_ARGB32);
            int n = 0;
            for (int y = 0; y < img.height(); y += 2)
                for (int x = 0; x < img.width(); x += 2) {
                    const QColor c = img.pixelColor(x, y);
                    n += c.alpha() > 200 && std::abs(c.red() - 0xe8) < 12 && std::abs(c.green() - 0x56) < 12
                         && std::abs(c.blue() - 0x6f) < 12;
                }
            return n;
        };
        OverlayWindow overlay;
        overlay.setStyle(Style::Lens); // no symbols, which are also tinted with the highlight colour
        overlay.setColors(QColor(0xee, 0x6e, 0x55), QColor(0xe8, 0x56, 0x6f), QColor(0xb8, 0x39, 0x2f));
        const LyricDocument doc = synthetic();
        overlay.setLyrics(doc);
        const LyricWord &w = doc.lines[0].words[2];
        overlay.setFrame(0, 2, w.endMs); // sung word, sweep complete
        QVERIFY(highlightPixels(render(overlay, {900, 520})) > 200);
        overlay.setFrame(0, -1, doc.lines[0].endMs); // gap after the line: no word is sung
        QTest::qWait(900);
        QCOMPARE(highlightPixels(overlay.view()->grabWindow()), 0);
    }

    void textScaleAndFontApply()
    {
        OverlayWindow overlay;
        overlay.setBackdrop(false);
        overlay.setStyle(Style::Drift);
        overlay.setLyrics(synthetic());
        overlay.setFrame(1, 2);
        const double normal = coverage(render(overlay, {900, 260}));
        overlay.setTextAppearance(u"No Such Font Family"_s, 1.4); // unknown family falls back silently
        QTest::qWait(900);
        const double larger = coverage(overlay.view()->grabWindow());
        QVERIFY2(larger > normal * 1.3, qPrintable(u"%1 vs %2"_s.arg(larger).arg(normal)));
    }

    void introCardShowsBeforeTheFirstWord()
    {
        OverlayWindow overlay;
        overlay.setBackdrop(false);
        QImage art(64, 64, QImage::Format_RGB32);
        art.fill(QColor(40, 120, 200));
        overlay.setArtwork(art);
        overlay.setTrackInfo(u"Paper Boats"_s, u"The Lanterns"_s);
        const LyricDocument doc = synthetic();
        overlay.setLyrics(doc);
        overlay.setFrame(-1, -1);
        QVERIFY(coverage(render(overlay, {900, 520})) > 0.01); // title, artist and art tile
        auto *card = overlay.view()->rootObject()->findChild<QQuickItem *>(u"introCard"_s);
        QVERIFY(card);
        QTRY_COMPARE(card->opacity(), 1.0);
        overlay.setFrame(0, 0, doc.lines[0].words[0].startMs); // singing starts: the card gives way
        QTRY_COMPARE(card->opacity(), 0.0);
        overlay.setTrackInfo({}, {}); // no track: no card
        overlay.setLyrics({});
        QTest::qWait(900);
        QCOMPARE(coverage(overlay.view()->grabWindow()), 0.0);
    }

    void artBackdropNeedsArtWordsAndEffects()
    {
        OverlayWindow overlay;
        auto *art = overlay.view()->rootObject()->findChild<QQuickItem *>(u"artBackdrop"_s);
        QVERIFY(art);
        QImage cover(64, 64, QImage::Format_RGB32);
        cover.fill(QColor(30, 90, 160));
        overlay.setArtwork(cover);
        overlay.setArtBackdrop(true);
        const LyricDocument doc = synthetic();
        overlay.setLyrics(doc);
        overlay.setFrame(0, 1, doc.lines[0].words[1].startMs);
        render(overlay, {640, 400});
        QVERIFY(!art->isVisible()); // software renderer: no blur available, so no art layer
        QCOMPARE(art->property("wanted").toBool(), true); // but it is requested
        overlay.setArtBackdrop(false);
        QCOMPARE(art->property("wanted").toBool(), false);
        overlay.setArtBackdrop(true);
        overlay.setArtwork({});
        QCOMPARE(art->property("wanted").toBool(), false); // no cover, no art layer
    }

    void continuousDriftRunsOnlyWhenWantedAndPlaying()
    {
        OverlayWindow overlay;
        QQuickItem *root = overlay.view()->rootObject();
        const LyricDocument doc = synthetic();
        overlay.setLyrics(doc);
        overlay.setFrame(0, 1, doc.lines[0].words[1].startMs);
        overlay.setStyle(Style::Drift);
        overlay.setContinuousDrift(true);
        overlay.setPlaying(true);
        render(overlay, {640, 400});
        QVERIFY(root->property("drifting").toBool());
        overlay.setPlaying(false); // paused: no frames
        QVERIFY(!root->property("drifting").toBool());
        overlay.setPlaying(true);
        overlay.setStyle(Style::Glyphs); // other styles never drift
        QVERIFY(!root->property("drifting").toBool());
        overlay.setStyle(Style::Drift);
        overlay.setContinuousDrift(false);
        QVERIFY(!root->property("drifting").toBool());
    }

    void instrumentalBreakShowsNotes()
    {
        OverlayWindow overlay;
        QQuickItem *root = overlay.view()->rootObject();
        // "one two" is sung in ~1.6 s, then nothing until 12 s: a long break.
        const LyricDocument doc = WordTimingSynthesizer::expand(
            std::get<LyricDocument>(LrcParser::parse(u"[00:01.00]one two\n[00:12.00]three four\n"_s)));
        overlay.setLyrics(doc);
        overlay.setPlaying(true);
        overlay.setFrame(0, -1, 3000);
        QVERIFY(root->property("instrumental").toBool());
        auto *notes = root->findChild<QQuickItem *>(u"breakNotes"_s);
        QVERIFY(notes);
        render(overlay, {640, 400});
        QVERIFY(notes->isVisible());
        overlay.setFrame(0, -1, 9000); // 3 s left: not a break worth showing
        QVERIFY(!root->property("instrumental").toBool());
        overlay.setFrame(0, -1, 3000);
        QVERIFY(root->property("instrumental").toBool());
        overlay.setFrame(1, 0, 12'000); // singing resumes
        QVERIFY(!root->property("instrumental").toBool());
    }

    void instrumentalBreakEndsBeforeTheNextLine()
    {
        OverlayWindow overlay;
        QQuickItem *root = overlay.view()->rootObject();
        const LyricDocument doc = WordTimingSynthesizer::expand(
            std::get<LyricDocument>(LrcParser::parse(u"[00:01.00]one\n[00:07.00]two\n"_s)));
        overlay.setLyrics(doc);
        overlay.setPlaying(true);
        overlay.setFrame(0, -1, 2400); // 4.6 s to go: shown, then cleared 1.5 s before "two"
        QVERIFY(root->property("instrumental").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!root->property("instrumental").toBool(), 5000);
    }

    void attributionIsShownWithLyrics()
    {
        OverlayWindow overlay;
        QQuickItem *root = overlay.view()->rootObject();
        LyricDocument doc = synthetic();
        doc.provider = u"Spicy Lyrics community"_s;
        doc.credits = {{u"synced by gc"_s, QUrl()}, {u"uploaded by spikerko"_s, QUrl()}};
        overlay.setLyrics(doc);
        QCOMPARE(root->property("attribution").toString(),
                 u"Lyrics: Spicy Lyrics community · synced by gc · uploaded by spikerko"_s);
        auto *credit = root->findChild<QQuickItem *>(u"attributionText"_s);
        QVERIFY(credit);
        overlay.setFrame(0, 1, doc.lines[0].words[1].startMs);
        render(overlay, {800, 480});
        QVERIFY(credit->isVisible()); // on screen whenever lyrics are
        overlay.setLyrics({});
        QVERIFY(root->property("attribution").toString().isEmpty());
    }

    void karaokeFillsTheCurrentLine()
    {
        OverlayWindow overlay;
        overlay.setBackdrop(false);
        overlay.setStyle(Style::Karaoke);
        QQuickItem *root = overlay.view()->rootObject();
        const LyricDocument doc = synthetic();
        overlay.setLyrics(doc);
        overlay.setFrame(1, 2, doc.lines[1].words[2].startMs); // "Lanterns glow [above] ..."
        QVERIFY(coverage(render(overlay, {900, 520})) > 0.01);
        auto *view = root->findChild<QQuickItem *>(u"karaokeView"_s);
        QVERIFY(view && view->isVisible());
        QCOMPARE(root->property("karaokeLine").toInt(), 1);
        // Progress per word of the current line: sung, sung, partly, not yet.
        QCOMPARE(root->property("karaokeProgress").toList().value(0).toDouble(), 1.0);
        QCOMPARE(root->property("karaokeProgress").toList().value(1).toDouble(), 1.0);
        QVERIFY(root->property("karaokeProgress").toList().value(3).toDouble() == 0.0);
        overlay.setFrame(1, -1, doc.lines[1].endMs); // line finished: all filled
        QTRY_COMPARE(root->property("karaokeProgress").toList().value(5).toDouble(), 1.0);
    }

    void backdropShowsOnlyWithWords()
    {
        OverlayWindow overlay;
        overlay.setBackdrop(true);
        const QImage idle = render(overlay, {900, 520});
        QCOMPARE(coverage(idle), 0.0); // no words: nothing at all, backdrop included
        const LyricDocument doc = synthetic();
        overlay.setLyrics(doc);
        overlay.setFrame(0, 3, doc.lines[0].words[3].startMs);
        QTest::qWait(900);
        const double withBackdrop = coverage(overlay.view()->grabWindow());
        overlay.setBackdrop(false);
        QTest::qWait(900);
        const double without = coverage(overlay.view()->grabWindow());
        QVERIFY2(withBackdrop > without + 0.1, qPrintable(u"%1 vs %2"_s.arg(withBackdrop).arg(without)));
    }

    void styleNamesAreOriginalAndDistinct()
    {
        QStringList names;
        for (Style s : {Style::Drift, Style::Lens, Style::LensGlyphs, Style::Glyphs, Style::Karaoke})
            names << OverlayWindow::styleName(s);
        QCOMPARE(names, (QStringList{u"Drift"_s, u"Lens"_s, u"Lens + Glyphs"_s, u"Glyphs"_s, u"Karaoke"_s}));
    }
};

QTEST_MAIN(TestRender)
#include "tst_render.moc"
