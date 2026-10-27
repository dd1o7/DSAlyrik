#pragma once

#include <QDialog>

#include "lyrics/LyricTypes.h"

class AppSettings;
class QComboBox;
class QSpinBox;
class QLabel;
class QLineEdit;

// One window for every setting, bound live to AppSettings both ways: changes
// apply to the overlay immediately, and changes made elsewhere (the tray) show here.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(AppSettings *settings, QWidget *parent = nullptr);

    void setTimingContext(const QString &trackKey, const QString &trackLabel, const QString &playerId,
                          const QString &playerLabel);
    void setPlayers(const QStringList &services);
    void setLyricsCredits(const LyricDocument &document); // provider and community credits, with links

private:
    AppSettings *m_settings;
    QComboBox *m_playerBox = nullptr;
    QSpinBox *m_playerOffset = nullptr;
    QSpinBox *m_songOffset = nullptr;
    QLabel *m_credits = nullptr;
    QLabel *m_keyState = nullptr;
    QString m_trackKey, m_trackLabel, m_playerId, m_playerLabel;
};
