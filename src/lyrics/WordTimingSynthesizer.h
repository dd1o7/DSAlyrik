#pragma once

#include "lyrics/LyricTypes.h"

// Splits line-timed lyrics into words with approximate timing. Each word's
// share of the line is proportional to its estimated syllables (plus held notes
// and breath pauses), sung at a tempo estimated from the song's tightest lines, so generated
// timing is an estimate, not a measured word boundary. Lines that already
// carry words are left unchanged.
class WordTimingSynthesizer {
public:
    static LyricDocument expand(const LyricDocument &doc);
    static int syllables(const QString &word);
    static qint64 weightTenths(const QString &word);

    // A line is sung for at most this long per syllable (plus padding); the rest of
    // a long line, typically an instrumental break, stays a gap with no active word.
    static constexpr qint64 kMaxMsPerSyllable = 700; // ceiling; the song's own tempo usually sets less
    static constexpr qint64 kMinMsPerSyllable = 280;
    static constexpr qint64 kSpanPaddingMs = 400;
};
