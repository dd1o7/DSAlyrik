#pragma once

#include <QString>

// Pairs common lyric words with a Unicode symbol (rendered by the system
// emoji/symbol font; no image assets are bundled). Unknown words map to "".
class WordGlyphResolver {
public:
    static QString glyphFor(const QString &word);
    static int tableSize();               // number of mapped base words
    static QStringList duplicateWords();  // words listed under more than one symbol
    static QStringList symbols();
    // Symbol for a phrase ending at the last word (tries the last three, then two words).
    static QString glyphForPhrase(const QStringList &words, int *length = nullptr);
    static int phraseCount();
};
