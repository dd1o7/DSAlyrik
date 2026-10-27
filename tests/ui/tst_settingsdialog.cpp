#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QtTest>

#include "settings/AppSettings.h"
#include "ui/SettingsDialog.h"

using namespace Qt::StringLiterals;
using Style = OverlayWindow::Style;

class TestSettingsDialog : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;
    std::unique_ptr<QSettings> store() const
    {
        return std::make_unique<QSettings>(m_dir.filePath(u"s.ini"_s), QSettings::IniFormat);
    }
    template <typename T> static T *child(QWidget &w, const char *name)
    {
        T *c = w.findChild<T *>(QString::fromLatin1(name));
        if (!c)
            qFatal("missing control %s", name);
        return c;
    }

private slots:
    void controlsWriteSettings()
    {
        AppSettings s(store());
        SettingsDialog dialog(&s);
        child<QComboBox>(dialog, "style")->setCurrentIndex(int(Style::Lens));
        QCOMPARE(s.style(), Style::Lens);
        child<QSlider>(dialog, "textScale")->setValue(140);
        QCOMPARE(s.textScale(), 1.4);
        child<QCheckBox>(dialog, "artBackdrop")->setChecked(false);
        QVERIFY(!s.artBackdrop());
        child<QComboBox>(dialog, "screensaver")->setCurrentIndex(2); // 1 min
        QCOMPARE(s.idleDelaySec(), 60);
        child<QSpinBox>(dialog, "offsetAll")->setValue(-250);
        QCOMPARE(s.timingOffsetMs(), -250);
        child<QCheckBox>(dialog, "hideFullscreen")->setChecked(false);
        QVERIFY(!s.hideOverFullscreen());
    }

    void settingsUpdateControls()
    {
        AppSettings s(store());
        SettingsDialog dialog(&s);
        s.setStyle(Style::Drift);
        QCOMPARE(child<QComboBox>(dialog, "style")->currentIndex(), int(Style::Drift));
        s.setKeepAwake(false);
        QVERIFY(!child<QCheckBox>(dialog, "keepAwake")->isChecked());
        s.setTimingOffsetMs(300);
        QCOMPARE(child<QSpinBox>(dialog, "offsetAll")->value(), 300);
    }

    void spicyLyricsKeyAndCredits()
    {
        AppSettings s(store(), m_dir.filePath(u"key"_s));
        SettingsDialog dialog(&s);
        child<QLineEdit>(dialog, "spicyKey")->setText(u"sl_sk_typed"_s);
        child<QPushButton>(dialog, "spicyKeySave")->click();
        QCOMPARE(s.spicyLyricsKey(), u"sl_sk_typed"_s);
        QVERIFY(child<QLineEdit>(dialog, "spicyKey")->text().isEmpty()); // never echoed back
        LyricDocument doc;
        doc.provider = u"Spicy Lyrics community"_s;
        doc.credits = {{u"synced by gc"_s, QUrl(u"https://spicylyrics.org/uid/1"_s)}};
        dialog.setLyricsCredits(doc);
        const QString credits = child<QLabel>(dialog, "credits")->text();
        QVERIFY(credits.contains(u"Spicy Lyrics community"_s));
        QVERIFY(credits.contains(u"href=\"https://spicylyrics.org/uid/1\""_s));
        child<QPushButton>(dialog, "spicyKeyRemove")->click();
        QVERIFY(s.spicyLyricsKey().isEmpty());
    }

    void songAndPlayerTimingFollowContext()
    {
        AppSettings s(store());
        SettingsDialog dialog(&s);
        QVERIFY(!child<QSpinBox>(dialog, "offsetSong")->isEnabled()); // nothing playing
        dialog.setTimingContext(u"k9"_s, u"Song"_s, u"spotify"_s, u"Spotify"_s);
        QVERIFY(child<QSpinBox>(dialog, "offsetSong")->isEnabled());
        child<QSpinBox>(dialog, "offsetSong")->setValue(-120);
        QCOMPARE(s.trackOffsetMs(u"k9"_s), -120);
        child<QSpinBox>(dialog, "offsetPlayer")->setValue(80);
        QCOMPARE(s.playerOffsetMs(u"spotify"_s), 80);
    }
};

QTEST_MAIN(TestSettingsDialog)
#include "tst_settingsdialog.moc"
