#include "platform/FullscreenWatcher.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonValue>
#include <QStandardPaths>

namespace {

const QString kKWin = QStringLiteral("org.kde.KWin");
const QString kScripting = QStringLiteral("/Scripting");
const QString kScriptingInterface = QStringLiteral("org.kde.kwin.Scripting");

QDBusMessage scriptingCall(const QString &method, const QVariantList &args = {})
{
    auto msg = QDBusMessage::createMethodCall(kKWin, kScripting, kScriptingInterface, method);
    msg.setArguments(args);
    return QDBusConnection::sessionBus().call(msg, QDBus::Block, 2000);
}

QString jsString(const QString &text)
{
    // A JSON string literal is a valid, safely escaped JavaScript string literal.
    return QString::fromUtf8(QJsonDocument::fromVariant(QVariantList{text}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
}

} // namespace

FullscreenWatcher::FullscreenWatcher(QObject *parent) : QObject(parent)
{
    QDBusConnection::sessionBus().registerObject(kObjectPath, this, QDBusConnection::ExportScriptableSlots);
}

FullscreenWatcher::~FullscreenWatcher() { unload(); }

QString FullscreenWatcher::scriptSource(const QString &service)
{
    return QStringLiteral(R"JS(// DSAlyrik: report whether the active window is fullscreen (unloaded when DSAlyrik quits).
const service = %1;
function report() {
    const w = workspace.activeWindow;
    const fullscreen = !!(w && w.fullScreen && !w.desktopWindow);
    callDBus(service, %2, %3, "setActiveFullscreen", fullscreen);
}
let watched = null;
function onActivated(w) {
    if (watched)
        watched.fullScreenChanged.disconnect(report);
    watched = w;
    if (w)
        w.fullScreenChanged.connect(report);
    report();
}
workspace.windowActivated.connect(onActivated);
onActivated(workspace.activeWindow);
)JS")
        .arg(jsString(service), jsString(kObjectPath), jsString(kInterface));
}

void FullscreenWatcher::setEnabled(bool enabled)
{
    if (enabled) {
        load();
    } else {
        unload();
        setActiveFullscreen(false);
    }
}

void FullscreenWatcher::load()
{
    if (m_loaded)
        return;
    QDBusConnectionInterface *bus = QDBusConnection::sessionBus().interface();
    if (!bus || !bus->isServiceRegistered(kKWin))
        return; // not KDE: unavailable
    // Inside a Flatpak only $XDG_RUNTIME_DIR/app/<id> is shared with the host, where KWin reads the script.
    QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (const QString app = qEnvironmentVariable("FLATPAK_ID"); !app.isEmpty())
        dir += QStringLiteral("/app/") + app;
    QDir().mkpath(dir);
    m_scriptPath = dir + QStringLiteral("/dsalyrik-fullscreen.js");
    QFile file(m_scriptPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    file.write(scriptSource(QDBusConnection::sessionBus().baseService()).toUtf8());
    file.close();

    if (scriptingCall(QStringLiteral("isScriptLoaded"), {kPluginName}).arguments().value(0).toBool())
        scriptingCall(QStringLiteral("unloadScript"), {kPluginName}); // a leftover from a crashed run
    const QDBusMessage loaded = scriptingCall(QStringLiteral("loadScript"), {m_scriptPath, kPluginName});
    if (loaded.type() == QDBusMessage::ErrorMessage || loaded.arguments().value(0).toInt() < 0)
        return;
    scriptingCall(QStringLiteral("start"));
    m_loaded = true;
}

void FullscreenWatcher::unload()
{
    if (!m_loaded)
        return;
    scriptingCall(QStringLiteral("unloadScript"), {kPluginName});
    QFile::remove(m_scriptPath);
    m_loaded = false;
}

void FullscreenWatcher::setActiveFullscreen(bool active)
{
    if (active == m_active)
        return;
    m_active = active;
    emit fullscreenChanged(active);
}
