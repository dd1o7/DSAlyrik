#pragma once

#include <QObject>
#include <cstdint>

struct wl_display;
struct wl_registry;
struct wl_seat;
struct ext_idle_notifier_v1;
struct ext_idle_notification_v1;
class QSocketNotifier;

// Reports when the user has not touched keyboard or pointer for a while, through
// the Wayland ext-idle-notify protocol on a private connection (independent of
// the window's own platform, which may be XWayland). Event-driven: no polling.
// Uses input-only idleness when available, so DSAlyrik's own idle inhibitor does
// not hide user inactivity.
class IdleMonitor : public QObject {
    Q_OBJECT
public:
    explicit IdleMonitor(QObject *parent = nullptr);
    ~IdleMonitor() override;

    bool isAvailable() const { return m_notifier != nullptr; }
    bool isIdle() const { return m_idle; }
    void setTimeoutMs(int ms); // 0 = stop watching

    // Wayland listener callbacks (C function pointers, hence public statics).
    static void onGlobal(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t version);
    static void onGlobalRemove(void *, wl_registry *, uint32_t) {}
    static void onIdled(void *data, ext_idle_notification_v1 *);
    static void onResumed(void *data, ext_idle_notification_v1 *);

signals:
    void idleChanged(bool idle);

private:
    void connectToCompositor();
    void flush();
    void setIdle(bool idle);


    wl_display *m_display = nullptr;
    wl_registry *m_registry = nullptr;
    wl_seat *m_seat = nullptr;
    ext_idle_notifier_v1 *m_notifier = nullptr;
    uint32_t m_notifierVersion = 0;
    ext_idle_notification_v1 *m_notification = nullptr;
    QSocketNotifier *m_socket = nullptr;
    int m_timeoutMs = 0;
    bool m_idle = false;
};
