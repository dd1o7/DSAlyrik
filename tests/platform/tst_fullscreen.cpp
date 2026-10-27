#include <QDBusInterface>
#include <QSignalSpy>
#include <QtTest>

#include "platform/FullscreenWatcher.h"

using namespace Qt::StringLiterals;

class TestFullscreen : public QObject {
    Q_OBJECT

private slots:
    void scriptReportsToThisProcess()
    {
        const QString js = FullscreenWatcher::scriptSource(u":1.42"_s);
        QVERIFY(js.contains(u"\":1.42\""_s));
        QVERIFY(js.contains(FullscreenWatcher::kObjectPath));
        QVERIFY(js.contains(FullscreenWatcher::kInterface));
        QVERIFY(js.contains(u"fullScreenChanged"_s));
        QVERIFY(js.contains(u"windowActivated"_s));
    }

    void callbackFromTheCompositorChangesState()
    {
        FullscreenWatcher watcher; // no KWin on the private bus
        QVERIFY(!watcher.isAvailable());
        QSignalSpy changed(&watcher, &FullscreenWatcher::fullscreenChanged);
        // What KWin's callDBus does: call our object on our unique name.
        QDBusInterface self(QDBusConnection::sessionBus().baseService(), FullscreenWatcher::kObjectPath,
                            FullscreenWatcher::kInterface);
        self.call(u"setActiveFullscreen"_s, true);
        QTRY_COMPARE(changed.size(), 1);
        QVERIFY(watcher.isFullscreenActive());
        self.call(u"setActiveFullscreen"_s, true); // no change, no signal
        self.call(u"setActiveFullscreen"_s, false);
        QTRY_COMPARE(changed.size(), 2);
        QVERIFY(!watcher.isFullscreenActive());
    }
};

QTEST_GUILESS_MAIN(TestFullscreen)
#include "tst_fullscreen.moc"
