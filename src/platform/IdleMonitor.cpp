#include "platform/IdleMonitor.h"

#include <QSocketNotifier>
#include <cstring>
#include <wayland-client.h>

#include "ext-idle-notify-v1-client-protocol.h"

namespace {
const wl_registry_listener kRegistryListener = {&IdleMonitor::onGlobal, &IdleMonitor::onGlobalRemove};
const ext_idle_notification_v1_listener kNotificationListener = {&IdleMonitor::onIdled, &IdleMonitor::onResumed};
} // namespace

IdleMonitor::IdleMonitor(QObject *parent) : QObject(parent) { connectToCompositor(); }

IdleMonitor::~IdleMonitor()
{
    if (m_notification)
        ext_idle_notification_v1_destroy(m_notification);
    if (m_notifier)
        ext_idle_notifier_v1_destroy(m_notifier);
    if (m_seat)
        wl_seat_destroy(m_seat);
    if (m_registry)
        wl_registry_destroy(m_registry);
    if (m_display)
        wl_display_disconnect(m_display);
}

void IdleMonitor::connectToCompositor()
{
    if (qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
        return; // X11 session: not supported
    m_display = wl_display_connect(nullptr);
    if (!m_display)
        return;
    m_registry = wl_display_get_registry(m_display);
    wl_registry_add_listener(m_registry, &kRegistryListener, this);
    wl_display_roundtrip(m_display);
    if (!m_seat || !m_notifier) {
        // The compositor lacks the protocol: leave isAvailable() false.
        if (m_notifier) {
            ext_idle_notifier_v1_destroy(m_notifier);
            m_notifier = nullptr;
        }
        return;
    }
    m_socket = new QSocketNotifier(wl_display_get_fd(m_display), QSocketNotifier::Read, this);
    connect(m_socket, &QSocketNotifier::activated, this, [this] {
        if (wl_display_dispatch(m_display) < 0)
            m_socket->setEnabled(false); // compositor connection lost
    });
}

void IdleMonitor::onGlobal(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    auto *self = static_cast<IdleMonitor *>(data);
    if (std::strcmp(interface, wl_seat_interface.name) == 0 && !self->m_seat) {
        self->m_seat = static_cast<wl_seat *>(wl_registry_bind(registry, name, &wl_seat_interface, 1));
    } else if (std::strcmp(interface, ext_idle_notifier_v1_interface.name) == 0) {
        self->m_notifierVersion = std::min<uint32_t>(version, 2);
        self->m_notifier = static_cast<ext_idle_notifier_v1 *>(
            wl_registry_bind(registry, name, &ext_idle_notifier_v1_interface, self->m_notifierVersion));
    }
}

void IdleMonitor::onIdled(void *data, ext_idle_notification_v1 *) { static_cast<IdleMonitor *>(data)->setIdle(true); }
void IdleMonitor::onResumed(void *data, ext_idle_notification_v1 *) { static_cast<IdleMonitor *>(data)->setIdle(false); }

void IdleMonitor::setTimeoutMs(int ms)
{
    if (!isAvailable() || ms == m_timeoutMs)
        return;
    m_timeoutMs = ms;
    if (m_notification) {
        ext_idle_notification_v1_destroy(m_notification);
        m_notification = nullptr;
    }
    setIdle(false);
    if (ms > 0) {
        // Version 2: input idleness ignores idle inhibitors (including our own keep-awake).
        m_notification = m_notifierVersion >= 2
            ? ext_idle_notifier_v1_get_input_idle_notification(m_notifier, uint32_t(ms), m_seat)
            : ext_idle_notifier_v1_get_idle_notification(m_notifier, uint32_t(ms), m_seat);
        ext_idle_notification_v1_add_listener(m_notification, &kNotificationListener, this);
    }
    flush();
}

void IdleMonitor::flush()
{
    wl_display_flush(m_display);
    wl_display_dispatch_pending(m_display);
}

void IdleMonitor::setIdle(bool idle)
{
    if (idle == m_idle)
        return;
    m_idle = idle;
    emit idleChanged(idle);
}
