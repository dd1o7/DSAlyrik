#pragma once

#include <QString>
#include <QUrl>
#include <QVector>

struct LyricSyllable {
    QString text;
    qint64 startMs = 0;
    qint64 endMs = 0;
};

struct LyricWord {
    QString text;
    qint64 startMs = 0;
    qint64 endMs = 0;
    QVector<LyricSyllable> syllables; // from syllable-synced sources; empty otherwise
};

// Who to credit on screen for the lyrics being shown (required by some sources).
struct LyricCredit {
    QString label; // e.g. "synced by gc"
    QUrl url;      // profile link, when the source gives one
};

struct LyricLine {
    QString text;              // full line text; words may be empty until expanded
    QVector<LyricWord> words;
    qint64 startMs = 0;
    qint64 endMs = 0;
};

struct LyricDocument {
    QString title;
    QString artist;
    QVector<LyricLine> lines;
    bool estimatedTiming = false; // line times guessed from unsynced lyrics
    QString provider;             // shown on screen with the lyrics, e.g. "Apple Music via Spicy Lyrics"
    QVector<LyricCredit> credits; // community uploader / maker, when present
};
