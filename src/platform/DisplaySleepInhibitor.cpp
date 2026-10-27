#include "platform/DisplaySleepInhibitor.h"

#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

namespace {

const QString kPortalService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPortalPath = QStringLiteral("/org/freedesktop/portal/desktop");

// KDE's portal backend keeps an idle inhibition alive after its caller exits,
// so every release must be an explicit Close that actually reaches the bus.
void closeRequest(const QDBusConnection &bus, const QDBusObjectPath &handle, QDBus::CallMode mode = QDBus::NoBlock)
{
    const auto close = QDBusMessage::createMethodCall(kPortalService, handle.path(),
                                                      QStringLiteral("org.freedesktop.portal.Request"),
                                                      QStringLiteral("Close"));
    bus.call(close, mode, 1000);
}

} // namespace

DisplaySleepInhibitor::DisplaySleepInhibitor(const QDBusConnection &bus, QObject *parent)
    : QObject(parent), m_bus(bus), m_portalWatcher(kPortalService, bus, QDBusServiceWatcher::WatchForOwnerChange)
{
    connect(&m_portalWatcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString &, const QString &, const QString &newOwner) {
                // A restarted portal has forgotten our request.
                m_pending = false;
                if (isInhibiting()) {
                    m_handle = {};
                    emit inhibitionChanged(false);
                }
                if (!newOwner.isEmpty() && m_wanted)
                    acquire();
            });
}

DisplaySleepInhibitor::~DisplaySleepInhibitor()
{
    m_wanted = false;
    if (isInhibiting())
        closeRequest(m_bus, m_handle, QDBus::Block); // flush before the process exits
}

void DisplaySleepInhibitor::setActive(bool active, const QString &reason)
{
    if (!reason.isEmpty())
        m_reason = reason;
    m_wanted = active;
    active ? acquire() : release();
}

void DisplaySleepInhibitor::acquire()
{
    if (m_pending || isInhibiting())
        return;
    m_pending = true;
    auto call = QDBusMessage::createMethodCall(kPortalService, kPortalPath, QStringLiteral("org.freedesktop.portal.Inhibit"),
                                               QStringLiteral("Inhibit"));
    call << QString() << uint(kIdleFlag)
         << QVariantMap{{QStringLiteral("reason"), m_reason},
                        {QStringLiteral("handle_token"), QStringLiteral("dsalyrik%1").arg(++m_token)}};
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call, 5000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (!m_pending)
            return; // superseded by a portal restart
        m_pending = false;
        QDBusPendingReply<QDBusObjectPath> reply = *w;
        if (reply.isError()) {
            emit failed(tr("Could not keep the display awake: %1").arg(reply.error().message()));
            return;
        }
        if (!m_wanted) {
            closeRequest(m_bus, reply.value()); // deactivated while the request was in flight
            return;
        }
        m_handle = reply.value();
        emit inhibitionChanged(true);
    });
}

void DisplaySleepInhibitor::release()
{
    if (!isInhibiting())
        return;
    closeRequest(m_bus, m_handle);
    m_handle = {};
    emit inhibitionChanged(false);
}
