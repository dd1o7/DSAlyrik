#pragma once

#include <QObject>
#include <QString>

// Learns whether the active window is fullscreen (a game, a video) from a small
// KWin script loaded at runtime that calls back over D-Bus. KDE only: elsewhere
// isAvailable() is false and nothing is ever reported as fullscreen.
class FullscreenWatcher : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.dd1o7.DSAlyrik.Fullscreen")
public:
    explicit FullscreenWatcher(QObject *parent = nullptr);
    ~FullscreenWatcher() override;

    bool isAvailable() const { return m_loaded; }
    bool isFullscreenActive() const { return m_active; }
    void setEnabled(bool enabled); // loads or unloads the KWin script

    static QString scriptSource(const QString &service);
    static inline const QString kObjectPath = QStringLiteral("/DSAlyrikFullscreen");
    static inline const QString kInterface = QStringLiteral("io.github.dd1o7.DSAlyrik.Fullscreen");
    static inline const QString kPluginName = QStringLiteral("dsalyrik-fullscreen");

public slots:
    Q_SCRIPTABLE void setActiveFullscreen(bool active);

signals:
    void fullscreenChanged(bool active);

private:
    void load();
    void unload();

    bool m_loaded = false;
    bool m_active = false;
    QString m_scriptPath;
};
