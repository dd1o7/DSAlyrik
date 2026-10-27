#include "lyrics/TrackNameNormalizer.h"

#include <QRegularExpression>

namespace {

// Annotations that change nothing about the sung words or their timing.
// Live, acoustic and remix versions are deliberately kept: their timing differs.
const QRegularExpression &annotation()
{
    static const QRegularExpression re(
        QStringLiteral(R"(^\s*(feat\.?|ft\.?|featuring|with|from|theme from|music from)\b)"
                       R"(|\b(re-?master(ed)?|deluxe|bonus|explicit|clean|edition|version|edit|mono|stereo|anniversary)\b)"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

} // namespace

QString TrackNameNormalizer::cleanTitle(const QString &title)
{
    static const QRegularExpression bracketed(QStringLiteral(R"(\s*[\(\[]([^\(\)\[\]]*)[\)\]])"));
    static const QRegularExpression dashSuffix(QStringLiteral(R"(\s+[-–—]\s+([^-–—]+)$)"));

    QString out = title;
    // Repeat until stable: removing one annotation can expose another, e.g. "(Deluxe) (with X)".
    for (bool removed = true; removed;) {
        removed = false;
        QList<QRegularExpressionMatch> matches;
        for (auto it = bracketed.globalMatch(out); it.hasNext();)
            matches.prepend(it.next()); // right to left keeps earlier offsets valid
        for (const auto &match : std::as_const(matches)) {
            if (annotation().match(match.captured(1)).hasMatch()) {
                out.remove(match.capturedStart(), match.capturedLength());
                removed = true;
            }
        }
    }
    if (const auto m = dashSuffix.match(out); m.hasMatch() && annotation().match(m.captured(1)).hasMatch())
        out.truncate(m.capturedStart());

    out = out.simplified();
    return out.isEmpty() ? title.simplified() : out;
}

QString TrackNameNormalizer::primaryArtist(const QString &artist)
{
    static const QRegularExpression separator(
        QStringLiteral(R"(\s*[,;]\s*|\s+(feat\.?|ft\.?|featuring)\s+)"), QRegularExpression::CaseInsensitiveOption);
    const QString first = artist.split(separator, Qt::SkipEmptyParts).value(0).simplified();
    return first.isEmpty() ? artist.simplified() : first;
}

QString TrackNameNormalizer::comparable(const QString &text)
{
    QString out;
    for (QChar c : text.normalized(QString::NormalizationForm_KD)) {
        if (c.category() == QChar::Mark_NonSpacing)
            continue; // drop accents: café -> cafe
        out += c.isLetterOrNumber() ? c.toCaseFolded() : QChar(u' ');
    }
    return out.simplified();
}
