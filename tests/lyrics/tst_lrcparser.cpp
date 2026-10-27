#include <QtTest>

#include "lyrics/LrcParser.h"

class TestLrcParser : public QObject {
    Q_OBJECT

    static LyricDocument ok(QStringView text)
    {
        auto result = LrcParser::parse(text);
        if (auto *err = std::get_if<ParseError>(&result))
            qWarning() << "unexpected parse error:" << err->message;
        return std::get<LyricDocument>(result);
    }

private slots:
    void parsesTimestampsAndMetadata()
    {
        const auto doc = ok(u"[ti:Song]\n[ar:Band]\n[00:01.50]First line\n[01:02.345]Second\n[02:03]Third");
        QCOMPARE(doc.title, QStringLiteral("Song"));
        QCOMPARE(doc.artist, QStringLiteral("Band"));
        QCOMPARE(doc.lines.size(), 3);
        QCOMPARE(doc.lines[0].startMs, 1500);
        QCOMPARE(doc.lines[0].text, QStringLiteral("First line"));
        QCOMPARE(doc.lines[0].endMs, 62345);
        QCOMPARE(doc.lines[1].startMs, 62345);
        QCOMPARE(doc.lines[2].startMs, 123000);
        QCOMPARE(doc.lines[2].endMs, 123000 + LrcParser::kLastLineDurationMs);
    }

    void appliesOffsetTag()
    {
        // Positive offset shows lyrics earlier; results clamp at zero.
        const auto doc = ok(u"[offset:+500]\n[00:00.20]a\n[00:02.00]b");
        QCOMPARE(doc.lines[0].startMs, 0);
        QCOMPARE(doc.lines[1].startMs, 1500);
        const auto late = ok(u"[offset:-250]\n[00:01.00]a");
        QCOMPARE(late.lines[0].startMs, 1250);
    }

    void blankTimedLineEndsPreviousLine()
    {
        const auto doc = ok(u"[00:01.00]sing\n\n[00:03.00]\n[00:10.00]again");
        QCOMPARE(doc.lines.size(), 2);
        QCOMPARE(doc.lines[0].endMs, 3000);
        QCOMPARE(doc.lines[1].startMs, 10000);
    }

    void multipleTimestampsRepeatLineInOrder()
    {
        const auto doc = ok(u"[00:20.00]verse\n[00:10.00][00:30.00]chorus");
        QCOMPARE(doc.lines.size(), 3);
        QCOMPARE(doc.lines[0].text, QStringLiteral("chorus"));
        QCOMPARE(doc.lines[1].text, QStringLiteral("verse"));
        QCOMPARE(doc.lines[2].startMs, 30000);
    }

    void equalTimestampsKeepSourceOrder()
    {
        const auto doc = ok(u"[00:05.00]one\n[00:05.00]two");
        QCOMPARE(doc.lines.size(), 2);
        QCOMPARE(doc.lines[0].text, QStringLiteral("one"));
        QCOMPARE(doc.lines[1].text, QStringLiteral("two"));
    }

    void skipsMalformedLines()
    {
        const auto doc = ok(u"garbage\n[xx:yy]bad\n[00:75.00]bad seconds\n[00:01.00]good\n[00:02.00");
        QCOMPARE(doc.lines.size(), 1);
        QCOMPARE(doc.lines[0].text, QStringLiteral("good"));
    }

    void malformedOnlyInputIsAnError()
    {
        QVERIFY(std::holds_alternative<ParseError>(LrcParser::parse(u"no timing here\n[ar:x]")));
        QVERIFY(std::holds_alternative<ParseError>(LrcParser::parse(u"")));
        QVERIFY(std::holds_alternative<ParseError>(LrcParser::parse(u"[[[[:::]]]")));
    }

    void enhancedLrcGivesRealWordTiming()
    {
        const auto doc = ok(u"[00:10.00]<00:10.00>Hello <00:10.50>bright <00:11.20>world\n[00:13.00]next");
        const auto &w = doc.lines[0].words;
        QCOMPARE(doc.lines[0].text, QStringLiteral("Hello bright world"));
        QCOMPARE(w.size(), 3);
        QCOMPARE(w[0].text, QStringLiteral("Hello"));
        QCOMPARE(w[0].startMs, 10000);
        QCOMPARE(w[0].endMs, 10500);
        QCOMPARE(w[1].startMs, 10500);
        QCOMPARE(w[2].startMs, 11200);
        QCOMPARE(w[2].endMs, 13000);
        QVERIFY(doc.lines[1].words.isEmpty()); // plain lines stay for the synthesizer
    }

    void enhancedSyllablesJoinIntoWords()
    {
        const auto doc = ok(u"[00:01.00]<00:01.00>Hel<00:01.30>lo <00:01.60>you\n[00:03.00]x");
        const auto &w = doc.lines[0].words;
        QCOMPARE(w.size(), 2);
        QCOMPARE(w[0].text, QStringLiteral("Hello"));
        QCOMPARE(w[0].startMs, 1000);
        QCOMPARE(w[0].endMs, 1600);
        QCOMPARE(w[1].startMs, 1600);
    }

    void enhancedUntaggedWordsAreInterpolated()
    {
        const auto doc = ok(u"[00:01.00]<00:01.00>a b <00:02.00>c\n[00:03.00]x");
        const auto &w = doc.lines[0].words;
        QCOMPARE(w.size(), 3);
        QCOMPARE(w[1].startMs, 1500);
        QCOMPARE(w[0].endMs, 1500);
    }

    void enhancedWordsFollowOffset()
    {
        const auto doc = ok(u"[offset:+500]\n[00:02.00]<00:02.00>a <00:03.00>b\n[00:05.00]x");
        QCOMPARE(doc.lines[0].words[0].startMs, 1500);
        QCOMPARE(doc.lines[0].words[1].startMs, 2500);
    }

    void keepsUtf8Text()
    {
        const auto doc = ok(QString::fromUtf8("[00:01.00]こんにちは 世界\r\n[00:02.00]Ça va? 🎵"));
        QCOMPARE(doc.lines[0].text, QString::fromUtf8("こんにちは 世界"));
        QCOMPARE(doc.lines[1].text, QString::fromUtf8("Ça va? 🎵"));
    }
};

QTEST_GUILESS_MAIN(TestLrcParser)
#include "tst_lrcparser.moc"
