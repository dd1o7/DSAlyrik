#include <QtTest>

using namespace Qt::StringLiterals;

#include "lyrics/PlainLyricsTimer.h"
#include "lyrics/WordTimingSynthesizer.h"

class TestWordTiming : public QObject {
    Q_OBJECT

    static LyricDocument oneLine(const QString &text, qint64 start, qint64 end)
    {
        LyricDocument doc;
        LyricLine line;
        line.text = text;
        line.startMs = start;
        line.endMs = end;
        doc.lines.append(line);
        return doc;
    }

    static void verifyContiguous(const LyricLine &line)
    {
        QVERIFY(!line.words.isEmpty());
        QCOMPARE(line.words.first().startMs, line.startMs);
        QVERIFY(line.words.last().endMs <= line.endMs);
        for (qsizetype i = 0; i < line.words.size(); ++i) {
            QVERIFY(line.words[i].startMs <= line.words[i].endMs);
            if (i > 0)
                QCOMPARE(line.words[i].startMs, line.words[i - 1].endMs);
        }
    }

private slots:
    void splitsOnWhitespaceKeepingPunctuation()
    {
        const auto doc = WordTimingSynthesizer::expand(oneLine(u"  Hello,   big\tworld!  "_s, 1000, 2300));
        const auto &line = doc.lines[0];
        QCOMPARE(line.words.size(), 3);
        QCOMPARE(line.words[0].text, u"Hello,"_s);
        QCOMPARE(line.words[2].text, u"world!"_s);
        verifyContiguous(line);
        // Weights (tenths of a syllable): "Hello," 2 + comma pause 0.5 = 25, "big" 10,
        // "world!" 1 + end pause 0.8 = 18; 1300 ms split 25:10:18.
        QCOMPARE(line.words[0].endMs, 1000 + 1300 * 25 / 53);
        QCOMPARE(line.words[1].endMs, 1000 + 1300 * 35 / 53);
        QCOMPARE(line.words[2].endMs, 2300);
    }

    void longGapDoesNotStretchWords()
    {
        // 3 syllables are sung in a few seconds at most; a 20 s line leaves the rest as a gap.
        const auto doc = WordTimingSynthesizer::expand(oneLine(u"one more time"_s, 0, 20'000));
        verifyContiguous(doc.lines[0]);
        QVERIFY(doc.lines[0].words.last().endMs
                <= 3 * WordTimingSynthesizer::kMaxMsPerSyllable + WordTimingSynthesizer::kSpanPaddingMs);
    }

    void tempoComesFromTheSong()
    {
        // Four tight lines at ~300 ms per syllable, then a line followed by a long break.
        LyricDocument doc;
        for (int i = 0; i < 4; ++i)
            doc.lines.append({u"we go round again"_s, {}, i * 1200, (i + 1) * 1200}); // 4 syllables / 1.2 s
        doc.lines.append({u"we go round again"_s, {}, 4800, 16'000});
        const auto out = WordTimingSynthesizer::expand(doc);
        const qint64 sung = out.lines[4].words.last().endMs - out.lines[4].startMs;
        QVERIFY2(sung <= 2300, qPrintable(QString::number(sung))); // fixed 700 ms/syllable would give 3.2 s
        QVERIFY(sung >= 1200);
    }

    void punctuationAndHeldNotesTakeLonger()
    {
        const auto plain = WordTimingSynthesizer::expand(oneLine(u"wait now"_s, 0, 1000)).lines[0];
        const auto comma = WordTimingSynthesizer::expand(oneLine(u"wait, now"_s, 0, 1000)).lines[0];
        QVERIFY(comma.words[0].endMs > plain.words[0].endMs);
        const auto shortOh = WordTimingSynthesizer::expand(oneLine(u"oh yes"_s, 0, 1000)).lines[0];
        const auto longOh = WordTimingSynthesizer::expand(oneLine(u"ooooooh yes"_s, 0, 1000)).lines[0];
        QVERIFY(longOh.words[0].endMs > shortOh.words[0].endMs);
        const auto tilde = WordTimingSynthesizer::expand(oneLine(u"baby~ yes"_s, 0, 1000)).lines[0];
        const auto baby = WordTimingSynthesizer::expand(oneLine(u"baby yes"_s, 0, 1000)).lines[0];
        QVERIFY(tilde.words[0].endMs > baby.words[0].endMs);
    }

    void plainLyricsAreSpreadAcrossTheSong()
    {
        const QString plain = u"first line here\nsecond line\n\nafter the break\nlast one"_s;
        const LyricDocument doc = PlainLyricsTimer::estimate(plain, 200'000);
        QVERIFY(doc.estimatedTiming);
        QCOMPARE(doc.lines.size(), 4);
        QCOMPARE(doc.lines[0].startMs, 15'000); // intro: 8 % of 200 s, at most 15 s
        QVERIFY(doc.lines.last().endMs <= 200'000 - 12'000); // outro: 6 %
        for (qsizetype i = 1; i < doc.lines.size(); ++i)
            QVERIFY(doc.lines[i].startMs > doc.lines[i - 1].startMs);
        // The stanza break leaves a longer gap before "after the break".
        const qint64 gapBefore = doc.lines[2].startMs - doc.lines[1].startMs;
        const qint64 normal = doc.lines[1].startMs - doc.lines[0].startMs;
        QVERIFY(gapBefore > normal);
        QVERIFY(PlainLyricsTimer::estimate(plain, 0).lines.isEmpty()); // no length, no guess
        QVERIFY(PlainLyricsTimer::estimate(u"  \n"_s, 200'000).lines.isEmpty());
    }

    void countsSyllablesAcrossScripts()
    {
        QCOMPARE(WordTimingSynthesizer::syllables(u"hello"_s), 2);
        QCOMPARE(WordTimingSynthesizer::syllables(u"time"_s), 1); // silent trailing e
        QCOMPARE(WordTimingSynthesizer::syllables(u"rhythm"_s), 1);
        QCOMPARE(WordTimingSynthesizer::syllables(QString::fromUtf8("こんにちは")), 5);
        QCOMPARE(WordTimingSynthesizer::syllables(u"..."_s), 1);
    }

    void oneWordSpansLine()
    {
        const auto doc = WordTimingSynthesizer::expand(oneLine(u"Oh"_s, 500, 900));
        QCOMPARE(doc.lines[0].words.size(), 1);
        verifyContiguous(doc.lines[0]);
        QCOMPARE(doc.lines[0].words[0].endMs, 900);
    }

    void punctuationOnlyWordStillGetsTime()
    {
        const auto doc = WordTimingSynthesizer::expand(oneLine(u"wait ... go"_s, 0, 900));
        QCOMPARE(doc.lines[0].words.size(), 3);
        QVERIFY(doc.lines[0].words[1].endMs > doc.lines[0].words[1].startMs);
        verifyContiguous(doc.lines[0]);
    }

    void longWordsStayInBounds()
    {
        const QString longWord(5000, QLatin1Char('a'));
        const auto doc = WordTimingSynthesizer::expand(oneLine(longWord + u" b"_s, 0, 7));
        verifyContiguous(doc.lines[0]);
    }

    void zeroDurationLine()
    {
        const auto doc = WordTimingSynthesizer::expand(oneLine(u"quick words"_s, 4000, 4000));
        QCOMPARE(doc.lines[0].words.size(), 2);
        verifyContiguous(doc.lines[0]);
    }

    void emptyTextHasNoWords()
    {
        const auto doc = WordTimingSynthesizer::expand(oneLine(u"   "_s, 0, 1000));
        QVERIFY(doc.lines[0].words.isEmpty());
    }

    void keepsExistingWordTiming()
    {
        auto input = oneLine(u"a b"_s, 0, 1000);
        input.lines[0].words = {{u"a"_s, 0, 100}, {u"b"_s, 100, 1000}};
        const auto doc = WordTimingSynthesizer::expand(input);
        QCOMPARE(doc.lines[0].words[0].endMs, 100);
    }

    void isDeterministic()
    {
        const auto in = oneLine(u"the same input twice"_s, 123, 4567);
        const auto a = WordTimingSynthesizer::expand(in);
        const auto b = WordTimingSynthesizer::expand(in);
        for (qsizetype i = 0; i < a.lines[0].words.size(); ++i)
            QCOMPARE(a.lines[0].words[i].endMs, b.lines[0].words[i].endMs);
    }
};

QTEST_GUILESS_MAIN(TestWordTiming)
#include "tst_wordtiming.moc"
