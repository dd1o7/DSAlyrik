#pragma once

#include <QString>

// Cleans player-reported track metadata for lyric lookup: drops featured-artist,
// soundtrack-source and edition/remaster annotations, keeps meaningful
// parentheses such as "(I Can't Get No) Satisfaction".
class TrackNameNormalizer {
public:
    static QString cleanTitle(const QString &title);
    static QString primaryArtist(const QString &artist); // first of "A, B" / "A feat. B"
    static QString comparable(const QString &text);      // case/accent/punctuation-insensitive key
};
