#include <QSignalSpy>
#include <QtTest>

#include "FakePortal.h"
#include "platform/DisplaySleepInhibitor.h"

using namespace Qt::StringLiterals;

class TestInhibitor : public QObject {
    Q_OBJECT

private slots:
    void inhibitsIdleOnlyOnceWhileActive()
    {
        FakePortal portal;
        DisplaySleepInhibitor inhibitor;
        inhibitor.setActive(true, u"Showing lyrics"_s);
        QTRY_VERIFY(inhibitor.isInhibiting());
        inhibitor.setActive(true, u"Showing lyrics"_s);
        QTest::qWait(100);
        QCOMPARE(portal.inhibit.calls, 1);
        QCOMPARE(portal.inhibit.lastFlags, DisplaySleepInhibitor::kIdleFlag); // never logout/suspend/switch
        QCOMPARE(portal.inhibit.lastReason, u"Showing lyrics"_s);
    }

    void inactiveClosesTheRequest()
    {
        FakePortal portal;
        DisplaySleepInhibitor inhibitor;
        QSignalSpy changed(&inhibitor, &DisplaySleepInhibitor::inhibitionChanged);
        inhibitor.setActive(true, u"r"_s);
        QTRY_VERIFY(inhibitor.isInhibiting());
        inhibitor.setActive(false, {});
        QVERIFY(!inhibitor.isInhibiting());
        QTRY_COMPARE(portal.inhibit.closes, 1);
        QCOMPARE(changed.size(), 2);
    }

    void deactivateWhilePendingReleasesOnReply()
    {
        FakePortal portal;
        DisplaySleepInhibitor inhibitor;
        inhibitor.setActive(true, u"r"_s);
        inhibitor.setActive(false, {}); // before the portal answers
        QTRY_COMPARE(portal.inhibit.closes, 1);
        QVERIFY(!inhibitor.isInhibiting());
    }

    void failedRequestIsReported()
    {
        FakePortal portal;
        portal.inhibit.fail = true;
        DisplaySleepInhibitor inhibitor;
        QSignalSpy failed(&inhibitor, &DisplaySleepInhibitor::failed);
        inhibitor.setActive(true, u"r"_s);
        QTRY_COMPARE(failed.size(), 1);
        QVERIFY(!inhibitor.isInhibiting());
    }

    void noPortalIsReported()
    {
        DisplaySleepInhibitor inhibitor;
        QSignalSpy failed(&inhibitor, &DisplaySleepInhibitor::failed);
        inhibitor.setActive(true, u"r"_s);
        QTRY_COMPARE(failed.size(), 1);
        QVERIFY(!inhibitor.isInhibiting());
    }

    void portalLossDropsAndReacquires()
    {
        auto portal = std::make_unique<FakePortal>();
        DisplaySleepInhibitor inhibitor;
        inhibitor.setActive(true, u"r"_s);
        QTRY_VERIFY(inhibitor.isInhibiting());
        portal.reset(); // portal restarts: its requests are gone
        QTRY_VERIFY(!inhibitor.isInhibiting());
        portal = std::make_unique<FakePortal>();
        QTRY_VERIFY(inhibitor.isInhibiting()); // still wanted, so taken again
        QCOMPARE(portal->inhibit.calls, 1);
    }

    void destructionReleases()
    {
        FakePortal portal;
        {
            DisplaySleepInhibitor inhibitor;
            inhibitor.setActive(true, u"r"_s);
            QTRY_VERIFY(inhibitor.isInhibiting());
        }
        QTRY_COMPARE(portal.inhibit.closes, 1);
    }
};

QTEST_GUILESS_MAIN(TestInhibitor)
#include "tst_inhibitor.moc"
