#pragma once

#include <QMenu>
#include <QObject>
#include <QSystemTrayIcon>

class AppSettings;

// System tray (StatusNotifierItem on Plasma) menu. Every choice is written
// to AppSettings, whose typed signals drive the rest of the app.
class TrayController : public QObject {
    Q_OBJECT
public:
    explicit TrayController(AppSettings *settings, QObject *parent = nullptr);

    bool show(); // false when no system tray is available
    QMenu *menu() { return &m_menu; }
    void setPlayers(const QStringList &services, const QString &active);
    void setStatus(const QString &status);
    // The song and player the "This song" / "<Player>" timing items apply to.
    void setTimingContext(const QString &trackKey, const QString &trackLabel, const QString &playerId,
                          const QString &playerLabel);

    static QIcon appIcon();
    static QString playerLabel(const QString &service);
    static constexpr int kTimingStepMs = 100;
    void setScreensaverAvailable(bool available) { m_screensaverAvailable = available; }

    void setFullscreenLyrics(bool on) { m_fullscreenLyrics = on; }

signals:
    void quitRequested();
    void fullscreenLyricsToggled(bool on);
    void settingsRequested();

private:
    void rebuild();
    void pickColor(int which);
    void pickFont();

    AppSettings *m_settings;
    QSystemTrayIcon m_icon;
    QMenu m_menu;
    QStringList m_players;
    QString m_activePlayer;
    QString m_status;
    bool m_screensaverAvailable = true;
    bool m_fullscreenLyrics = false;
    QString m_trackKey, m_trackLabel, m_playerId, m_playerLabel;
};
