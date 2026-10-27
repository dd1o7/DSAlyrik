#include "lyrics/PlainLyricsTimer.h"

#include <QStringList>
#include <algorithm>

#include "lyrics/WordTimingSynthesizer.h"

LyricDocument PlainLyricsTimer::estimate(const QString &plainLyrics, qint64 durationMs)
{
    LyricDocument doc;
    if (durationMs <= 0)
        return doc;

    struct Planned {
        QString text;
        qint64 sungTenths = 0;
        qint64 pauseTenths = 0; // before this line
    };
    QVector<Planned> lines;
    bool stanzaBreak = false;
    for (const QString &raw : plainLyrics.split(u'\n')) {
        const QString text = raw.simplified();
        if (text.isEmpty()) {
            stanzaBreak = !lines.isEmpty();
            continue;
        }
        Planned p{text, 0, lines.isEmpty() ? 0 : (stanzaBreak ? 50 : 10)}; // a breath; a stanza is longer
        for (const QString &word : text.split(u' ', Qt::SkipEmptyParts))
            p.sungTenths += WordTimingSynthesizer::weightTenths(word);
        lines.append(p);
        stanzaBreak = false;
    }
    if (lines.isEmpty())
        return doc;

    const qint64 intro = std::min<qint64>(15'000, durationMs * 8 / 100);
    const qint64 outro = durationMs * 6 / 100;
    const qint64 span = std::max<qint64>(1, durationMs - intro - outro);
    qint64 totalTenths = 0;
    for (const Planned &p : lines)
        totalTenths += p.sungTenths + p.pauseTenths;

    qint64 cursorTenths = 0;
    for (const Planned &p : lines) {
        cursorTenths += p.pauseTenths;
        LyricLine line;
        line.text = p.text;
        line.startMs = intro + span * cursorTenths / totalTenths;
        cursorTenths += p.sungTenths;
        line.endMs = intro + span * cursorTenths / totalTenths;
        doc.lines.append(line);
    }
    doc.estimatedTiming = true;
    return doc;
}
