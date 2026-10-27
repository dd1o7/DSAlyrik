#include "ui/TrayController.h"

#include <QActionGroup>
#include <functional>
#include <QColorDialog>
#include <QFontDialog>
#include <QGuiApplication>
#include <QScreen>

#include "playback/MprisPlayer.h"
#include "settings/AppSettings.h"

using namespace Qt::StringLiterals;

namespace {
QMenu *submenu(QMenu &parent, const QString &title, const QString &objectName)
{
    QMenu *menu = parent.addMenu(title);
    menu->setObjectName(objectName);
    return menu;
}
} // namespace

TrayController::TrayController(AppSettings *settings, QObject *parent) : QObject(parent), m_settings(settings)
{
    connect(&m_menu, &QMenu::aboutToShow, this, &TrayController::rebuild);
    connect(&m_icon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger)
            m_settings->setOverlayVisible(!m_settings->overlayVisible());
        else if (reason == QSystemTrayIcon::DoubleClick || reason == QSystemTrayIcon::MiddleClick)
            emit settingsRequested();
    });
    rebuild();
}

QIcon TrayController::appIcon() { return QIcon(QStringLiteral(":/dsalyrik/icons/dsalyrik.svg")); }

QString TrayController::playerLabel(const QString &service)
{
    QString name = service.mid(Mpris::kServicePrefix.size()).section(u'.', 0, 0); // drop ".instance123"
    if (!name.isEmpty())
        name[0] = name[0].toUpper();
    return name;
}

bool TrayController::show()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return false;
    m_icon.setIcon(appIcon());
    m_icon.setToolTip(tr("DSAlyrik"));
    m_icon.setContextMenu(&m_menu);
    m_icon.show();
    return true;
}

void TrayController::setPlayers(const QStringList &services, const QString &active)
{
    m_players = services;
    m_activePlayer = active;
}

void TrayController::setTimingContext(const QString &trackKey, const QString &trackLabel, const QString &playerId,
                                      const QString &playerLabel)
{
    m_trackKey = trackKey;
    m_trackLabel = trackLabel;
    m_playerId = playerId;
    m_playerLabel = playerLabel;
}

void TrayController::setStatus(const QString &status)
{
    m_status = status;
    m_icon.setToolTip(status.isEmpty() ? tr("DSAlyrik") : tr("DSAlyrik — %1").arg(status));
}

void TrayController::rebuild()
{
    // Rebuilt each time the menu opens so players, displays and checks are current.
    m_menu.clear();
    for (QMenu *old : m_menu.findChildren<QMenu *>(Qt::FindDirectChildrenOnly))
        delete old; // not deleteLater: stale submenus must not outlive this rebuild

    QAction *status = m_menu.addAction(m_status.isEmpty() ? tr("Nothing playing") : m_status);
    status->setEnabled(false);
    m_menu.addSeparator();

    QAction *visible = m_menu.addAction(tr("Show lyrics"));
    visible->setCheckable(true);
    visible->setChecked(m_settings->overlayVisible());
    connect(visible, &QAction::triggered, m_settings, &AppSettings::setOverlayVisible);

    QAction *fullscreenLyrics = m_menu.addAction(tr("Fullscreen lyrics"));
    fullscreenLyrics->setCheckable(true);
    fullscreenLyrics->setChecked(m_fullscreenLyrics);
    connect(fullscreenLyrics, &QAction::triggered, this, &TrayController::fullscreenLyricsToggled);

    QAction *fullscreen = m_menu.addAction(tr("Hide over fullscreen apps"));
    fullscreen->setCheckable(true);
    fullscreen->setChecked(m_settings->hideOverFullscreen());
    connect(fullscreen, &QAction::triggered, m_settings, &AppSettings::setHideOverFullscreen);

    // Screensaver mode: lyrics only after the desktop has been idle, full screen.
    QMenu *saver = submenu(m_menu, tr("Screensaver mode"), QStringLiteral("screensaverMenu"));
    saver->setEnabled(m_screensaverAvailable);
    if (!m_screensaverAvailable)
        saver->setTitle(tr("Screensaver mode (needs Wayland idle support)"));
    auto *saverGroup = new QActionGroup(saver);
    const QList<std::pair<QString, int>> delays{{tr("Off: show while playing"), 0}, {tr("After 30 s idle"), 30},
                                                {tr("After 1 min idle"), 60}, {tr("After 2 min idle"), 120},
                                                {tr("After 5 min idle"), 300}};
    for (const auto &[label, seconds] : delays) {
        QAction *a = saver->addAction(label);
        a->setCheckable(true);
        a->setChecked(m_settings->idleDelaySec() == seconds);
        saverGroup->addAction(a);
        connect(a, &QAction::triggered, m_settings, [this, seconds] { m_settings->setIdleDelaySec(seconds); });
    }

    QAction *awake = m_menu.addAction(tr("Keep display awake while playing"));
    awake->setCheckable(true);
    awake->setChecked(m_settings->keepAwake());
    connect(awake, &QAction::triggered, m_settings, &AppSettings::setKeepAwake);

    QAction *artBackdrop = m_menu.addAction(tr("Album art backdrop"));
    artBackdrop->setCheckable(true);
    artBackdrop->setChecked(m_settings->artBackdrop());
    connect(artBackdrop, &QAction::triggered, m_settings, &AppSettings::setArtBackdrop);

    QAction *backdrop = m_menu.addAction(tr("Soft backdrop"));
    backdrop->setCheckable(true);
    backdrop->setChecked(m_settings->backdrop());
    connect(backdrop, &QAction::triggered, m_settings, &AppSettings::setBackdrop);

    QMenu *styles = submenu(m_menu, tr("Style"), QStringLiteral("styleMenu"));
    auto *styleGroup = new QActionGroup(styles);
    for (auto style : {OverlayWindow::Style::Drift, OverlayWindow::Style::Lens, OverlayWindow::Style::LensGlyphs,
                       OverlayWindow::Style::Glyphs, OverlayWindow::Style::Karaoke}) {
        QAction *a = styles->addAction(OverlayWindow::styleName(style));
        a->setCheckable(true);
        a->setChecked(m_settings->style() == style);
        styleGroup->addAction(a);
        connect(a, &QAction::triggered, m_settings, [this, style] { m_settings->setStyle(style); });
    }

    styles->addSeparator();
    QAction *drift = styles->addAction(tr("Keep Drift moving (uses more power)"));
    drift->setCheckable(true);
    drift->setChecked(m_settings->continuousDrift());
    connect(drift, &QAction::triggered, m_settings, &AppSettings::setContinuousDrift);

    QMenu *colors = submenu(m_menu, tr("Colors"), QStringLiteral("colorMenu"));
    QAction *autoSync = colors->addAction(tr("Auto Sync colors from album art"));
    autoSync->setCheckable(true);
    autoSync->setChecked(m_settings->autoSync());
    connect(autoSync, &QAction::triggered, m_settings, &AppSettings::setAutoSync);
    colors->addSeparator();
    const QStringList colorNames{tr("Text color…"), tr("Highlight color…"), tr("Secondary color…")};
    for (int i = 0; i < colorNames.size(); ++i)
        connect(colors->addAction(colorNames[i]), &QAction::triggered, this, [this, i] { pickColor(i); });
    connect(colors->addAction(tr("Reset colors")), &QAction::triggered, m_settings, [this] {
        m_settings->setColors(AppSettings::kDefaultText, AppSettings::kDefaultHighlight, AppSettings::kDefaultSecondary);
    });

    QMenu *text = submenu(m_menu, tr("Text"), QStringLiteral("textMenu"));
    connect(text->addAction(tr("Larger text")), &QAction::triggered, m_settings,
            [this] { m_settings->setTextScale(m_settings->textScale() + 0.1); });
    connect(text->addAction(tr("Smaller text")), &QAction::triggered, m_settings,
            [this] { m_settings->setTextScale(m_settings->textScale() - 0.1); });
    connect(text->addAction(tr("Reset text size")), &QAction::triggered, m_settings,
            [this] { m_settings->setTextScale(1.0); });
    text->addSeparator();
    connect(text->addAction(tr("Choose font…")), &QAction::triggered, this, &TrayController::pickFont);
    connect(text->addAction(tr("Default font (Nunito)")), &QAction::triggered, m_settings,
            [this] { m_settings->setFontFamily({}); });

    QMenu *timing = submenu(m_menu, tr("Timing"), QStringLiteral("timingMenu"));
    const auto signedMs = [](int ms) { return (ms > 0 ? u"+"_s : QString()) + QString::number(ms) + u" ms"_s; };
    // One section per scope: the playing song, the playing player, and everything.
    const auto addScope = [&](const QString &prefix, int current, bool enabled, std::function<void(int)> set) {
        for (const auto &[label, delta] : {std::pair{tr("earlier"), -kTimingStepMs}, std::pair{tr("later"), kTimingStepMs}}) {
            QAction *a = timing->addAction(tr("%1: %2").arg(prefix, label));
            a->setEnabled(enabled);
            connect(a, &QAction::triggered, m_settings, [set, current, delta] { set(current + delta); });
        }
        QAction *reset = timing->addAction(current == 0 ? tr("%1: reset").arg(prefix)
                                                        : tr("%1: reset (%2)").arg(prefix, signedMs(current)));
        reset->setEnabled(enabled && current != 0);
        connect(reset, &QAction::triggered, m_settings, [set] { set(0); });
    };
    timing->addSection(m_trackLabel.isEmpty() ? tr("This song") : tr("This song — %1").arg(m_trackLabel));
    addScope(tr("This song"), m_settings->trackOffsetMs(m_trackKey), !m_trackKey.isEmpty(),
             [this, key = m_trackKey](int ms) { m_settings->setTrackOffsetMs(key, ms); });
    const QString player = m_playerLabel.isEmpty() ? tr("This player") : m_playerLabel;
    timing->addSection(player);
    addScope(player, m_settings->playerOffsetMs(m_playerId), !m_playerId.isEmpty(),
             [this, id = m_playerId](int ms) { m_settings->setPlayerOffsetMs(id, ms); });
    timing->addSection(tr("All songs"));
    connect(timing->addAction(tr("Show lyrics earlier")), &QAction::triggered, m_settings,
            [this] { m_settings->setTimingOffsetMs(m_settings->timingOffsetMs() - kTimingStepMs); });
    connect(timing->addAction(tr("Show lyrics later")), &QAction::triggered, m_settings,
            [this] { m_settings->setTimingOffsetMs(m_settings->timingOffsetMs() + kTimingStepMs); });
    const int offset = m_settings->timingOffsetMs();
    QAction *resetTiming = timing->addAction(
        offset == 0 ? tr("Reset timing") : tr("Reset timing (%1%2 ms)").arg(offset > 0 ? u"+"_s : QString()).arg(offset));
    resetTiming->setEnabled(offset != 0);
    connect(resetTiming, &QAction::triggered, m_settings, [this] { m_settings->setTimingOffsetMs(0); });

    QMenu *displays = submenu(m_menu, tr("Displays"), QStringLiteral("displayMenu"));
    const auto screens = QGuiApplication::screens();
    for (qsizetype i = 0; i < screens.size(); ++i) {
        const QString name = screens[i]->name();
        QAction *a = displays->addAction(name.isEmpty() ? tr("Display %1").arg(i + 1) : name);
        a->setCheckable(true);
        a->setChecked(m_settings->isDisplayEnabled(name));
        a->setEnabled(!name.isEmpty()); // an unnamed screen cannot be remembered
        connect(a, &QAction::toggled, m_settings, [this, name](bool on) { m_settings->setDisplayEnabled(name, on); });
    }

    QMenu *players = submenu(m_menu, tr("Player"), QStringLiteral("playerMenu"));
    auto *playerGroup = new QActionGroup(players);
    QStringList services{QString()};
    services << m_players;
    for (const QString &service : std::as_const(services)) {
        QString label = service.isEmpty() ? tr("Automatic") : playerLabel(service);
        if (!service.isEmpty() && service == m_activePlayer && m_settings->preferredPlayer().isEmpty())
            label = tr("%1 (playing)").arg(label);
        QAction *a = players->addAction(label);
        a->setCheckable(true);
        a->setChecked(m_settings->preferredPlayer() == service);
        playerGroup->addAction(a);
        connect(a, &QAction::triggered, m_settings, [this, service] { m_settings->setPreferredPlayer(service); });
    }

    m_menu.addSeparator();
    connect(m_menu.addAction(tr("Settings…")), &QAction::triggered, this, &TrayController::settingsRequested);
    connect(m_menu.addAction(tr("Quit DSAlyrik")), &QAction::triggered, this, &TrayController::quitRequested);
}

void TrayController::pickFont()
{
    bool ok = false;
    QFont current;
    if (!m_settings->fontFamily().isEmpty())
        current.setFamily(m_settings->fontFamily());
    const QFont picked = QFontDialog::getFont(&ok, current, nullptr, tr("Choose the lyric font"));
    if (ok)
        m_settings->setFontFamily(picked.family());
}

void TrayController::pickColor(int which)
{
    QColor colors[] = {m_settings->textColor(), m_settings->highlightColor(), m_settings->secondaryColor()};
    const QColor picked = QColorDialog::getColor(colors[which], nullptr, tr("Choose a lyric color"));
    if (!picked.isValid())
        return;
    colors[which] = picked;
    m_settings->setColors(colors[0], colors[1], colors[2]);
}
