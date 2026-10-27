#include <QTemporaryDir>
#include <QtTest>
#include <optional>

#include "lyrics/FallbackLyricProvider.h"
#include "lyrics/LocalLyricProvider.h"

using namespace Qt::StringLiterals;

namespace {

LyricResult run(LyricProvider &provider, const LyricQuery &query)
{
    std::optional<LyricResult> result;
    provider.fetch(query, [&](const LyricResult &r) { result = r; });
    if (!QTest::qWaitFor([&] { return result.has_value(); }, 3000))
        qFatal("no result");
    return *result;
}

// Scripted provider for the chain tests.
class StubProvider : public LyricProvider {
public:
    explicit StubProvider(LyricResult::Kind kind) : m_kind(kind) {}
    RequestId fetch(const LyricQuery &, Completion completion) override
    {
        ++calls;
        const RequestId id = ++m_next;
        QTimer::singleShot(0, this, [this, id, completion] {
            if (cancelled.contains(id))
                return;
            LyricResult r;
            r.kind = m_kind;
            if (m_kind == LyricResult::Kind::Timed)
                r.document.lines.append({u"stub"_s, {}, 0, 1000});
            completion(r);
        });
        return id;
    }
    void cancel(RequestId id) override { cancelled << id; }
    int calls = 0;
    QList<RequestId> cancelled;

private:
    LyricResult::Kind m_kind;
    RequestId m_next = 0;
};

} // namespace

class TestLocalProvider : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;

private slots:
    void readsSidecarLrcNextToTheTrack()
    {
        QFile lrc(m_dir.filePath(u"Song.lrc"_s));
        QVERIFY(lrc.open(QIODevice::WriteOnly));
        lrc.write("[00:01.00]<00:01.00>hello <00:01.50>there\n[00:03.00]end\n");
        lrc.close();
        LocalLyricProvider provider;
        LyricQuery q{u"Song"_s, u"Band"_s, {}, 0};
        q.localUrl = QUrl::fromLocalFile(m_dir.filePath(u"Song.flac"_s));
        const LyricResult r = run(provider, q);
        QCOMPARE(r.kind, LyricResult::Kind::Timed);
        QCOMPARE(r.document.lines[0].words.size(), 2); // real word timing from the file
    }

    void missingOrRemoteMeansNoMatch()
    {
        LocalLyricProvider provider;
        LyricQuery q{u"Song"_s, u"Band"_s, {}, 0};
        q.localUrl = QUrl::fromLocalFile(m_dir.filePath(u"Other.mp3"_s));
        QCOMPARE(run(provider, q).kind, LyricResult::Kind::NoMatch);
        q.localUrl = QUrl(u"https://example.com/a.mp3"_s);
        QCOMPARE(run(provider, q).kind, LyricResult::Kind::NoMatch);
        q.localUrl = QUrl();
        QCOMPARE(run(provider, q).kind, LyricResult::Kind::NoMatch);
    }

    void chainFallsBackOnlyWhenNeeded()
    {
        StubProvider local(LyricResult::Kind::NoMatch), remote(LyricResult::Kind::Timed);
        FallbackLyricProvider chain({&local, &remote});
        QCOMPARE(run(chain, {u"a"_s, u"b"_s, {}, 0}).kind, LyricResult::Kind::Timed);
        QCOMPARE(local.calls, 1);
        QCOMPARE(remote.calls, 1);

        StubProvider hit(LyricResult::Kind::Timed), unused(LyricResult::Kind::Timed);
        FallbackLyricProvider chain2({&hit, &unused});
        QCOMPARE(run(chain2, {u"a"_s, u"b"_s, {}, 0}).kind, LyricResult::Kind::Timed);
        QCOMPARE(unused.calls, 0);
    }

    void chainCancelReachesTheActiveStep()
    {
        StubProvider local(LyricResult::Kind::NoMatch), remote(LyricResult::Kind::Timed);
        FallbackLyricProvider chain({&local, &remote});
        bool called = false;
        const RequestId id = chain.fetch({u"a"_s, u"b"_s, {}, 0}, [&](const LyricResult &) { called = true; });
        QTRY_COMPARE(remote.calls, 1); // local missed, remote is now active
        chain.cancel(id);
        QCOMPARE(remote.cancelled.size(), 1);
        QTest::qWait(50);
        QVERIFY(!called);
    }
};

QTEST_GUILESS_MAIN(TestLocalProvider)
#include "tst_localprovider.moc"
