#include "lyrics/FallbackLyricProvider.h"

#include <QPointer>

FallbackLyricProvider::FallbackLyricProvider(QList<LyricProvider *> chain, QObject *parent)
    : LyricProvider(parent), m_chain(std::move(chain))
{
}

RequestId FallbackLyricProvider::fetch(const LyricQuery &query, Completion completion)
{
    const RequestId id = ++m_next;
    m_requests.insert(id, {query, std::move(completion), 0, 0});
    start(id);
    return id;
}

void FallbackLyricProvider::start(RequestId id)
{
    Request &request = m_requests[id];
    LyricProvider *provider = m_chain.at(request.step);
    request.inner = provider->fetch(request.query, [self = QPointer(this), id](const LyricResult &result) {
        if (!self || !self->m_requests.contains(id))
            return; // cancelled
        Request &r = self->m_requests[id];
        const bool last = r.step + 1 >= self->m_chain.size();
        if (result.kind == LyricResult::Kind::Timed || last) {
            const Completion completion = r.completion;
            self->m_requests.remove(id);
            completion(result);
            return;
        }
        ++r.step;
        self->start(id);
    });
}

void FallbackLyricProvider::cancel(RequestId id)
{
    const auto it = m_requests.find(id);
    if (it == m_requests.end())
        return;
    m_chain.at(it->step)->cancel(it->inner);
    m_requests.erase(it);
}
