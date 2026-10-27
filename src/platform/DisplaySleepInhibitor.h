#pragma once

#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QDBusServiceWatcher>
#include <QObject>

// Keeps the display from idling (blanking/dimming) through the XDG Desktop
// Portal Inhibit API with only the Idle flag: suspend and session locking are
// never inhibited. Holds at most one request; releases it on deactivate,
// portal loss and destruction.
class DisplaySleepInhibitor : public QObject {
    Q_OBJECT
public:
    explicit DisplaySleepInhibitor(const QDBusConnection &bus = QDBusConnection::sessionBus(), QObject *parent = nullptr);
    ~DisplaySleepInhibitor() override;

    void setActive(bool active, const QString &reason);
    bool isInhibiting() const { return !m_handle.path().isEmpty(); }

    static constexpr uint kIdleFlag = 8;

signals:
    void inhibitionChanged(bool inhibiting);
    void failed(const QString &message);

private:
    void acquire();
    void release();

    QDBusConnection m_bus;
    QDBusServiceWatcher m_portalWatcher;
    QDBusObjectPath m_handle;
    QString m_reason;
    bool m_wanted = false;
    bool m_pending = false;
    quint64 m_token = 0;
};
