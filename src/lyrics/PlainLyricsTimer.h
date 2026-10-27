#pragma once

#include "lyrics/LyricTypes.h"

// Gives unsynced (plain) lyrics an estimated line timing across the track:
// after an intro, before an outro, weighted by syllables, with longer pauses at
// stanza breaks. Marked estimatedTiming; an approximation, not a sync.
class PlainLyricsTimer {
public:
    static LyricDocument estimate(const QString &plainLyrics, qint64 durationMs);
};
