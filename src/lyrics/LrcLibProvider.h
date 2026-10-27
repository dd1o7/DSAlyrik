#pragma once

#include <QHash>
#include <QNetworkAccessManager>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>
#include <QUrlQuery>
#include <optional>

#include "lyrics/LyricProvider.h"

class QNetworkReply;

// Line-timed lyrics from the LRCLIB API (see docs/lyrics-providers.md).
// An exact-signature miss falls back to one /api/search with a cleaned title,
// scored by title, artist and duration. Successful lookups and confirmed misses
// are cached in memory only.
class LrcLibProvider : public LyricProvider {
    Q_OBJECT
public:
    explicit LrcLibProvider(const QUrl &baseUrl = QUrl(QStringLiteral("https://lrclib.net")), QObject *parent = nullptr);

    RequestId fetch(const LyricQuery &query, Completion completion) override;
    void cancel(RequestId id) override;

    void setTimeoutMs(int ms) { m_timeoutMs = ms; }
    static QString cacheKey(const LyricQuery &query);
    static QByteArray userAgent();

private:
    struct Pending {
        QNetworkReply *reply = nullptr;
        Completion completion;
        QString cacheKey;
        LyricQuery query;
        bool searched = false; // the exact lookup missed and /api/search was asked
    };

    void deliverLater(RequestId id, const LyricResult &result);
    void handleReply(RequestId id);
    void startRequest(RequestId id, const QString &path, const QUrlQuery &params);
    LyricResult interpret(QNetworkReply *reply, const Pending &pending);
    static LyricResult fromRecord(const QJsonObject &record, qint64 durationMs);
    static std::optional<QJsonObject> bestCandidate(const QJsonArray &records, const LyricQuery &query);

    QNetworkAccessManager m_network;
    QUrl m_baseUrl;
    int m_timeoutMs = 8000;
    RequestId m_nextId = 1;
    QHash<RequestId, Pending> m_pending;
    QHash<QString, LyricResult> m_cache;
    qint64 m_blockedUntilMs = 0;
};
