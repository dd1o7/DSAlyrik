#include "lyrics/WordTimingSynthesizer.h"

#include <QRegularExpression>
#include <algorithm>

namespace {

bool isLatinVowel(QChar c)
{
    static const QString vowels = QStringLiteral("aeiouyàáâãäåèéêëìíîïòóôõöùúûüýÿæœ");
    return vowels.contains(c);
}

} // namespace

// Weight of a word in tenths of a syllable: its syllables, plus held notes
// (stretched letters, "~") and a short hold before punctuation (the breath).
qint64 WordTimingSynthesizer::weightTenths(const QString &word)
{
    qint64 tenths = qint64(syllables(word)) * 10;
    const QString lower = word.toLower();
    qint64 held = 0;
    for (qsizetype i = 0; i < lower.size();) {
        qsizetype run = 1;
        while (i + run < lower.size() && lower.at(i + run) == lower.at(i))
            ++run;
        if (lower.at(i).isLetter() && run >= 3)
            held += (run - 2) * 5; // "oooooh": each extra letter adds half a syllable
        i += run;
    }
    held += lower.count(u'~') * 10;
    tenths += std::min<qint64>(held, 30);
    const QChar last = word.isEmpty() ? QChar() : word.back();
    if (last == u',' || last == u'-' || last == u'—')
        tenths += 5;
    else if (QStringLiteral(".;:!?…").contains(last))
        tenths += 8;
    return tenths;
}

LyricDocument WordTimingSynthesizer::expand(const LyricDocument &doc)
{
    static const QRegularExpression whitespace(QStringLiteral(R"(\s+)"));

    struct Plan {
        QStringList tokens;
        QVector<qint64> weights;
        qint64 total = 0;
    };
    QVector<Plan> plans(doc.lines.size());
    QVector<double> rates; // ms per syllable of each line, for the song's tempo
    for (qsizetype i = 0; i < doc.lines.size(); ++i) {
        const LyricLine &line = doc.lines[i];
        if (!line.words.isEmpty())
            continue;
        Plan &plan = plans[i];
        plan.tokens = line.text.split(whitespace, Qt::SkipEmptyParts);
        for (const QString &token : std::as_const(plan.tokens)) {
            plan.weights.append(weightTenths(token));
            plan.total += plan.weights.last();
        }
        if (plan.total > 0 && line.endMs > line.startMs)
            rates.append(double(line.endMs - line.startMs) * 10.0 / double(plan.total));
    }
    // Tight lines show the real singing speed; lines before breaks only stretch it.
    qint64 msPerSyllable = kMaxMsPerSyllable;
    if (!rates.isEmpty()) {
        std::sort(rates.begin(), rates.end());
        const double typical = rates[qsizetype((rates.size() - 1) * 0.3)];
        msPerSyllable = std::clamp<qint64>(qint64(typical * 1.5), kMinMsPerSyllable, kMaxMsPerSyllable);
    }

    LyricDocument out = doc;
    for (qsizetype i = 0; i < out.lines.size(); ++i) {
        LyricLine &line = out.lines[i];
        const Plan &plan = plans[i];
        if (!line.words.isEmpty() || plan.tokens.isEmpty())
            continue;
        // Sing at the song's pace; a longer line ends in a gap with no active word.
        const qint64 duration = std::min(std::max<qint64>(0, line.endMs - line.startMs),
                                          plan.total * msPerSyllable / 10 + kSpanPaddingMs);
        qint64 cumulative = 0;
        qint64 cursor = line.startMs;
        for (qsizetype w = 0; w < plan.tokens.size(); ++w) {
            cumulative += plan.weights[w];
            const qint64 end = line.startMs + duration * cumulative / plan.total;
            line.words.append({plan.tokens[w], cursor, end});
            cursor = end;
        }
    }
    return out;
}

int WordTimingSynthesizer::syllables(const QString &word)
{
    int vowelGroups = 0;
    int otherUnits = 0; // non-Latin letters (kana, hangul, CJK) and digits count one each
    bool inVowel = false;
    for (QChar c : word.toLower()) {
        if (c.isLetter() && c.unicode() < 0x250) {
            const bool vowel = isLatinVowel(c);
            vowelGroups += vowel && !inVowel;
            inVowel = vowel;
        } else {
            otherUnits += c.isLetterOrNumber();
            inVowel = false;
        }
    }
    // English silent trailing "e" (time, more), but not "-le" (little).
    const QString letters = word.toLower().remove(QRegularExpression(QStringLiteral("[^a-z]")));
    if (vowelGroups > 1 && letters.endsWith(u'e') && !letters.endsWith(QStringLiteral("le")))
        --vowelGroups;
    return std::max(1, vowelGroups + otherUnits);
}
