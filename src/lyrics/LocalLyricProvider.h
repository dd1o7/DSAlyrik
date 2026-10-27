#pragma once

#include <QHash>

#include "lyrics/LyricProvider.h"

// Reads a sidecar LRC file next to a local track (Song.flac -> Song.lrc), which
// may carry real word timing. Only local file URLs are considered.
class LocalLyricProvider : public LyricProvider {
    Q_OBJECT
public:
    using LyricProvider::LyricProvider;
    RequestId fetch(const LyricQuery &query, Completion completion) override;
    void cancel(RequestId id) override { m_pending.remove(id); }

    static constexpr qint64 kMaxFileBytes = 1024 * 1024;

private:
    RequestId m_next = 0;
    QHash<RequestId, Completion> m_pending;
};
