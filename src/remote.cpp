#include "remote.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QFileInfo>
#include <QUrl>
#include <QWindow>

#include "backend.h"
#include "windowmanager.h"

QString Remote::serviceName() {
    return QStringLiteral("io.omacom.Omawrite");
}

QString Remote::objectPath() {
    return QStringLiteral("/io/omacom/Omawrite");
}

Remote::Remote(WindowManager *windows, QObject *parent)
    : QObject(parent), m_windows(windows) {}

bool Remote::claim(WindowManager *windows) {
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;

    // Registering the name is the whole single-instance test: the bus hands it
    // to one process, and the one that misses out knows to hand its request on.
    if (!bus.registerService(serviceName()))
        return false;

    auto *remote = new Remote(windows, windows);
    if (!bus.registerObject(objectPath(), remote,
                            QDBusConnection::ExportAllSlots)) {
        bus.unregisterService(serviceName());
        return false;
    }
    return true;
}

bool Remote::requestOpen(const QString &path, int line) {
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;

    QDBusInterface omawrite(serviceName(), objectPath(), serviceName(), bus);
    if (!omawrite.isValid())
        return false;

    const QDBusReply<bool> reply = omawrite.call(QStringLiteral("OpenFile"), path, line);
    return reply.isValid() && reply.value();
}

bool Remote::OpenFile(const QString &path, int line) {
    if (!m_windows)
        return false;

    Backend *backend = m_windows->primaryBackend();
    if (!backend)
        return false;

    // Relative paths are the caller's, not ours: they were typed in a shell
    // that may be anywhere, so the client resolves them before sending.
    const QFileInfo target(path);
    if (!target.isAbsolute())
        return false;

    backend->openAtLine(QUrl::fromLocalFile(target.absoluteFilePath()), line);

    // Showing a file nobody can see is not showing it, so the window comes
    // forward with it.
    if (QWindow *window = backend->parentWindow()) {
        window->show();
        window->raise();
        window->requestActivate();
    }
    return true;
}
