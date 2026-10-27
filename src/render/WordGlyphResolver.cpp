#include "render/WordGlyphResolver.h"

#include <QFile>
#include <QHash>
#include <QStringList>

namespace {

struct GlyphTable {
    QHash<QString, QString> byWord;
    QHash<QString, QString> byPhrase; // "broken heart" -> symbol
    QStringList duplicates;
    QStringList symbols;
};

// resources/glyphs.txt: "<symbol>\t<word> <word> ..." per line, '#' comments.
const GlyphTable &table()
{
    static const GlyphTable loaded = [] {
        GlyphTable t;
        QFile file(QStringLiteral(":/dsalyrik/glyphs.txt"));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return t;
        const QString text = QString::fromUtf8(file.readAll());
        for (const QString &line : text.split(u'\n')) {
            if (line.isEmpty() || line.startsWith(u'#'))
                continue;
            const QString symbol = line.section(u'\t', 0, 0).trimmed();
            const QStringList words = line.section(u'\t', 1).split(u' ', Qt::SkipEmptyParts);
            if (symbol.isEmpty() || words.isEmpty())
                continue;
            t.symbols << symbol;
            for (const QString &word : words) {
                if (t.byWord.contains(word))
                    t.duplicates << word;
                else
                    t.byWord.insert(word, symbol);
            }
        }
        QFile phrases(QStringLiteral(":/dsalyrik/phrases.txt"));
        if (phrases.open(QIODevice::ReadOnly | QIODevice::Text)) {
            for (const QString &line : QString::fromUtf8(phrases.readAll()).split(u'\n')) {
                if (line.isEmpty() || line.startsWith(u'#'))
                    continue;
                const QString symbol = line.section(u'\t', 0, 0).trimmed();
                if (!t.symbols.contains(symbol))
                    t.symbols << symbol;
                for (const QString &phrase : line.section(u'\t', 1).split(u'|', Qt::SkipEmptyParts))
                    t.byPhrase.insert(phrase.simplified().toLower(), symbol);
            }
        }
        return t;
    }();
    return loaded;
}

// Lowercase letters and apostrophes only: "Heart." -> "heart", "rock n' roll" keeps "n'".
QString phraseWord(const QString &word)
{
    QString out;
    for (QChar c : word.toLower()) {
        if (c == u'’')
            c = u'\'';
        if (c.isLetter() || c == u'\'')
            out += c;
    }
    return out;
}

bool isVowel(QChar c) { return QStringLiteral("aeiou").contains(c); }

// Base-form candidates for an inflected word, most likely first.
QStringList lemmas(const QString &w)
{
    QStringList out{w};
    const auto add = [&](const QString &stem) {
        if (stem.size() >= 2)
            out << stem;
    };
    const auto withVariants = [&](const QString &stem) {
        add(stem);
        add(stem + u'e'); // danc(ing) -> dance, lov(ed) -> love
        if (stem.size() >= 3 && stem.back() == stem.at(stem.size() - 2) && !isVowel(stem.back()))
            add(stem.chopped(1)); // runn(ing) -> run, stopp(ed) -> stop
    };
    if (w.endsWith(QStringLiteral("ies")))
        add(w.chopped(3) + u'y');
    if (w.endsWith(QStringLiteral("es")))
        add(w.chopped(2));
    if (w.endsWith(u's') && !w.endsWith(QStringLiteral("ss")))
        add(w.chopped(1));
    if (w.endsWith(QStringLiteral("ing")))
        withVariants(w.chopped(3));
    if (w.endsWith(QStringLiteral("ied")))
        add(w.chopped(3) + u'y'); // cried -> cry
    if (w.endsWith(QStringLiteral("ed")))
        withVariants(w.chopped(2));
    if (w.endsWith(QStringLiteral("est")))
        withVariants(w.chopped(3));
    if (w.endsWith(QStringLiteral("er")))
        withVariants(w.chopped(2));
    if (w.endsWith(QStringLiteral("ly")))
        add(w.chopped(2));
    return out;
}

} // namespace

QString WordGlyphResolver::glyphFor(const QString &word)
{
    // Keep letters and apostrophes; unify typographic apostrophes.
    QString key;
    for (QChar c : word.toLower()) {
        if (c == u'’' || c == u'‘')
            c = u'\'';
        if (c.isLetter() || c == u'\'')
            key += c;
    }
    while (key.startsWith(u'\''))
        key.remove(0, 1);
    while (key.endsWith(u'\''))
        key.chop(1);
    if (key.isEmpty())
        return {};

    // Contractions: keep the word for pronoun/possessive forms (you're, night's),
    // never for negations or auxiliaries (won't != won, I'll != I).
    if (const qsizetype apostrophe = key.indexOf(u'\''); apostrophe >= 0) {
        const QString suffix = key.mid(apostrophe + 1);
        static const QStringList keep{QStringLiteral("s"), QStringLiteral("re"), QStringLiteral("ve")};
        if (!keep.contains(suffix))
            return {};
        key.truncate(apostrophe);
    }

    const auto &words = table().byWord;
    for (const QString &candidate : lemmas(key))
        if (const auto it = words.constFind(candidate); it != words.cend())
            return *it;
    return {};
}

QString WordGlyphResolver::glyphForPhrase(const QStringList &words, int *length)
{
    const auto &phrases = table().byPhrase;
    for (int n = std::min<int>(3, int(words.size())); n >= 2; --n) {
        QStringList parts;
        for (qsizetype i = words.size() - n; i < words.size(); ++i)
            parts << phraseWord(words[i]);
        if (const auto it = phrases.constFind(parts.join(u' ')); it != phrases.cend()) {
            if (length)
                *length = n;
            return *it;
        }
    }
    return {};
}

int WordGlyphResolver::phraseCount() { return int(table().byPhrase.size()); }

int WordGlyphResolver::tableSize() { return int(table().byWord.size()); }
QStringList WordGlyphResolver::duplicateWords() { return table().duplicates; }
QStringList WordGlyphResolver::symbols() { return table().symbols; }
