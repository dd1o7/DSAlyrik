#include "lyrics/LocalLyricProvider.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>

#include "lyrics/LrcParser.h"

namespace {

// Song.flac -> Song.lrc / Song.LRC in the same folder.
QString sidecarFor(const QUrl &url)
{
    if (!url.isLocalFile())
        return {};
    const QFileInfo track(url.toLocalFile());
    for (const QString &ext : {QStringLiteral("lrc"), QStringLiteral("LRC")}) {
        const QString candidate = track.dir().filePath(track.completeBaseName() + u'.' + ext);
        if (QFileInfo::exists(candidate))
            return candidate;
    }
    return {};
}

LyricResult readSidecar(const QString &path)
{
    LyricResult r;
    QFile file(path);
    if (path.isEmpty() || file.size() > LocalLyricProvider::kMaxFileBytes || !file.open(QIODevice::ReadOnly)) {
        r.message = QStringLiteral("no local lyrics file");
        return r;
    }
    auto parsed = LrcParser::parse(QString::fromUtf8(file.readAll()));
    if (auto *error = std::get_if<ParseError>(&parsed)) {
        r.kind = LyricResult::Kind::InvalidResponse;
        r.message = error->message;
        return r;
    }
    r.kind = LyricResult::Kind::Timed;
    r.document = std::get<LyricDocument>(std::move(parsed));
    r.document.provider = QStringLiteral("local .lrc file");
    return r;
}

} // namespace

RequestId LocalLyricProvider::fetch(const LyricQuery &query, Completion completion)
{
    const RequestId id = ++m_next;
    m_pending.insert(id, std::move(completion));
    const QString path = sidecarFor(query.localUrl);
    QTimer::singleShot(0, this, [this, id, path] {
        const Completion completion = m_pending.take(id); // empty if cancelled
        if (completion)
            completion(readSidecar(path));
    });
    return id;
}
