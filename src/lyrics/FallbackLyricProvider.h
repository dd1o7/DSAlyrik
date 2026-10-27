#pragma once

#include <QHash>
#include <QList>

#include "lyrics/LyricProvider.h"

// Tries providers in order and returns the first Timed result. A provider error on
// one step does not stop the chain; the last step's result is returned otherwise.
class FallbackLyricProvider : public LyricProvider {
    Q_OBJECT
public:
    explicit FallbackLyricProvider(QList<LyricProvider *> chain, QObject *parent = nullptr);
    RequestId fetch(const LyricQuery &query, Completion completion) override;
    void cancel(RequestId id) override;

private:
    struct Request {
        LyricQuery query;
        Completion completion;
        int step = 0;
        RequestId inner = 0;
    };
    void start(RequestId id);

    QList<LyricProvider *> m_chain;
    QHash<RequestId, Request> m_requests;
    RequestId m_next = 0;
};
