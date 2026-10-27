#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>

// Minimal org.freedesktop.portal.Inhibit + Request served from its own connection.
class FakeRequest : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Request")
public:
    explicit FakeRequest(int *closes) : m_closes(closes) {}
public slots:
    Q_SCRIPTABLE void Close() { ++*m_closes; }
private:
    int *m_closes;
};

class FakeInhibit : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Inhibit")
public:
    explicit FakeInhibit(QDBusConnection conn) : m_conn(conn) {}
    int calls = 0;
    int closes = 0;
    uint lastFlags = 0;
    QString lastReason;
    bool fail = false;

public slots:
    Q_SCRIPTABLE QDBusObjectPath Inhibit(const QString &, uint flags, const QVariantMap &options)
    {
        ++calls;
        lastFlags = flags;
        lastReason = options.value(QStringLiteral("reason")).toString();
        if (fail) {
            sendErrorReply(QStringLiteral("org.freedesktop.portal.Error.NotAllowed"), QStringLiteral("denied"));
            return {};
        }
        const QString path = QStringLiteral("/org/freedesktop/portal/desktop/request/test/r%1").arg(calls);
        auto *request = new FakeRequest(&closes);
        request->setParent(this);
        m_conn.registerObject(path, request, QDBusConnection::ExportScriptableSlots);
        return QDBusObjectPath(path);
    }

private:
    QDBusConnection m_conn;
};

class FakePortal {
public:
    FakePortal()
        : m_conn(QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("fake-portal"))),
          inhibit(m_conn)
    {
        m_conn.registerObject(QStringLiteral("/org/freedesktop/portal/desktop"), &inhibit,
                              QDBusConnection::ExportScriptableSlots);
        m_conn.registerService(QStringLiteral("org.freedesktop.portal.Desktop"));
    }
    ~FakePortal()
    {
        m_conn.unregisterService(QStringLiteral("org.freedesktop.portal.Desktop"));
        QDBusConnection::disconnectFromBus(m_conn.name());
    }

private:
    QDBusConnection m_conn;

public:
    FakeInhibit inhibit;
};
