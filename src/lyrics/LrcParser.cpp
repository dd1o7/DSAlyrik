#include "lyrics/LrcParser.h"

#include <QRegularExpression>
#include <algorithm>
#include <optional>

namespace {

struct Entry {
    qint64 timeMs;
    QString text;
    QVector<LyricWord> words; // from enhanced-LRC <mm:ss.xx> tags; ends filled in later
};

// Accepts mm:ss, mm:ss.f, mm:ss.ff and mm:ss.fff (':' also seen as separator).
std::optional<qint64> parseTimestamp(QStringView tag)
{
    static const QRegularExpression re(QStringLiteral(R"(^(\d{1,3}):(\d{1,2})(?:[.:](\d{1,3}))?$)"));
    const auto m = re.matchView(tag);
    if (!m.hasMatch())
        return std::nullopt;
    const qint64 minutes = m.capturedView(1).toLongLong();
    const qint64 seconds = m.capturedView(2).toLongLong();
    if (seconds >= 60)
        return std::nullopt;
    qint64 fractionMs = 0;
    if (const auto fraction = m.capturedView(3); !fraction.isEmpty()) {
        fractionMs = fraction.toLongLong();
        for (qsizetype digits = fraction.size(); digits < 3; ++digits)
            fractionMs *= 10;
    }
    return (minutes * 60 + seconds) * 1000 + fractionMs;
}

// Enhanced LRC ("A2"): <mm:ss.xx> tags inside the line time the word or syllable that
// follows. Chunks join into words at whitespace; a word without its own tag (-1) is
// interpolated later. Returns the plain text and the words (end times unset).
QString parseEnhanced(const QString &line, qint64 lineTimeMs, QVector<LyricWord> &words)
{
    static const QRegularExpression tag(QStringLiteral(R"(<(\d{1,3}:\d{1,2}(?:[.:]\d{1,3})?)>)"));
    if (!line.contains(tag))
        return line;

    QString plain;
    LyricWord current{QString(), -1, -1};
    const auto finish = [&] {
        if (!current.text.isEmpty())
            words.append(current);
        current = {QString(), -1, -1};
    };
    qint64 chunkTime = lineTimeMs;
    bool atChunkStart = true;
    qsizetype pos = 0;
    auto it = tag.globalMatch(line);
    const auto consume = [&](QStringView chunk) {
        for (QChar c : chunk) {
            if (c.isSpace()) {
                finish();
                if (!plain.isEmpty() && !plain.endsWith(u' '))
                    plain += u' ';
            } else {
                if (current.text.isEmpty())
                    current.startMs = atChunkStart ? chunkTime : -1;
                current.text += c;
                plain += c;
            }
            atChunkStart = false;
        }
    };
    while (it.hasNext()) {
        const auto m = it.next();
        consume(QStringView(line).sliced(pos, m.capturedStart() - pos));
        if (auto t = parseTimestamp(m.capturedView(1))) {
            chunkTime = *t;
            atChunkStart = true;
        }
        pos = m.capturedEnd();
    }
    consume(QStringView(line).sliced(pos));
    finish();
    return plain.trimmed();
}

} // namespace

std::variant<LyricDocument, ParseError> LrcParser::parse(QStringView text)
{
    static const QRegularExpression metaTag(QStringLiteral(R"(^([A-Za-z]+):(.*)$)"));

    LyricDocument doc;
    QVector<Entry> entries;
    qint64 offsetMs = 0;

    for (QStringView raw : text.tokenize(u'\n')) {
        QStringView rest = raw.trimmed();
        QVector<qint64> times;
        bool malformed = false;

        while (rest.startsWith(u'[')) {
            const qsizetype close = rest.indexOf(u']');
            if (close < 0) {
                malformed = true;
                break;
            }
            const QStringView tag = rest.sliced(1, close - 1).trimmed();
            rest = rest.sliced(close + 1);
            if (auto time = parseTimestamp(tag)) {
                times.append(*time);
                continue;
            }
            const auto meta = metaTag.matchView(tag);
            if (!meta.hasMatch() || !times.isEmpty()) {
                malformed = true;
                break;
            }
            const QString key = meta.captured(1).toLower();
            const QString value = meta.captured(2).trimmed();
            if (key == u"ti")
                doc.title = value;
            else if (key == u"ar")
                doc.artist = value;
            else if (key == u"offset")
                offsetMs = value.toLongLong();
        }

        if (malformed || times.isEmpty())
            continue;
        const QString rawText = rest.trimmed().toString();
        for (qint64 t : times) {
            QVector<LyricWord> words;
            const QString lineText = parseEnhanced(rawText, t, words);
            entries.append({t, lineText, words});
        }
    }

    if (std::none_of(entries.cbegin(), entries.cend(), [](const Entry &e) { return !e.text.isEmpty(); }))
        return ParseError{QStringLiteral("no timed lyric lines found")};

    // Positive offset shows lyrics earlier (standard LRC meaning).
    for (Entry &e : entries) {
        e.timeMs = std::max<qint64>(0, e.timeMs - offsetMs);
        for (LyricWord &w : e.words)
            if (w.startMs >= 0)
                w.startMs = std::max<qint64>(0, w.startMs - offsetMs);
    }
    std::stable_sort(entries.begin(), entries.end(),
                     [](const Entry &a, const Entry &b) { return a.timeMs < b.timeMs; });

    // Blank timed entries are not lines; they only end the line before them.
    for (qsizetype i = 0; i < entries.size(); ++i) {
        if (entries[i].text.isEmpty())
            continue;
        LyricLine line;
        line.text = entries[i].text;
        line.startMs = entries[i].timeMs;
        line.endMs = i + 1 < entries.size() ? entries[i + 1].timeMs : line.startMs + kLastLineDurationMs;
        line.words = entries[i].words;
        if (!line.words.isEmpty()) {
            // Untagged words: interpolate between the surrounding known starts.
            for (qsizetype w = 0; w < line.words.size(); ++w) {
                if (line.words[w].startMs >= 0)
                    continue;
                const qint64 before = w > 0 ? line.words[w - 1].startMs : line.startMs;
                qsizetype next = w + 1;
                while (next < line.words.size() && line.words[next].startMs < 0)
                    ++next;
                const qint64 after = next < line.words.size() ? line.words[next].startMs : line.endMs;
                line.words[w].startMs = before + (after - before) / (next - w + 1);
            }
            for (qsizetype w = 0; w < line.words.size(); ++w) {
                line.words[w].startMs = std::clamp(line.words[w].startMs, line.startMs, line.endMs);
                line.words[w].endMs = w + 1 < line.words.size() ? std::max(line.words[w].startMs, line.words[w + 1].startMs)
                                                                 : std::max(line.words[w].startMs, line.endMs);
            }
        }
        doc.lines.append(line);
    }
    return doc;
}
