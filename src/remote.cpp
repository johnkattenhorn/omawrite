#include "remote.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

bool Remote::requestOpen(const QString &path, int line, bool newTab) {
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;

    QDBusInterface omawrite(serviceName(), objectPath(), serviceName(), bus);
    if (!omawrite.isValid())
        return false;

    const QDBusReply<bool> reply = omawrite.call(QStringLiteral("OpenFile"), path, line, newTab);
    return reply.isValid() && reply.value();
}


// A call to whoever holds the name. Nothing there is not an error here: the
// caller decides what to do about it, which for --open is to open a window and
// for the reading calls is to say so and stop.
static QDBusInterface runningOmawrite() {
    return QDBusInterface(Remote::serviceName(), Remote::objectPath(),
                          Remote::serviceName(), QDBusConnection::sessionBus());
}

bool Remote::isRunning() {
    QDBusConnection bus = QDBusConnection::sessionBus();
    return bus.isConnected() && runningOmawrite().isValid();
}

QString Remote::requestState() {
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return {};

    QDBusInterface omawrite = runningOmawrite();
    if (!omawrite.isValid())
        return {};

    const QDBusReply<QString> reply = omawrite.call(QStringLiteral("State"));
    return reply.isValid() ? reply.value() : QString();
}

QString Remote::requestText(const QString &target) {
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return {};

    QDBusInterface omawrite = runningOmawrite();
    if (!omawrite.isValid())
        return {};

    const QDBusReply<QString> reply = omawrite.call(QStringLiteral("ReadText"), target);
    return reply.isValid() ? reply.value() : QString();
}

bool Remote::requestSelect(const QString &target) {
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;

    QDBusInterface omawrite = runningOmawrite();
    if (!omawrite.isValid())
        return false;

    const QDBusReply<bool> reply = omawrite.call(QStringLiteral("SelectTab"), target);
    return reply.isValid() && reply.value();
}

bool Remote::requestPresent() {
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;

    QDBusInterface omawrite = runningOmawrite();
    if (!omawrite.isValid())
        return false;

    const QDBusReply<bool> reply = omawrite.call(QStringLiteral("Present"));
    return reply.isValid() && reply.value();
}

bool Remote::Present() {
    if (!m_windows)
        return false;

    Backend *backend = m_windows->primaryBackend();
    if (!backend)
        return false;

    QWindow *window = backend->parentWindow();
    if (!window)
        return false;

    window->show();
    window->raise();
    window->requestActivate();
    return true;
}

bool Remote::OpenFile(const QString &path, int line, bool newTab) {
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

    backend->openAtLine(QUrl::fromLocalFile(target.absoluteFilePath()), line, newTab);

    // Showing a file nobody can see is not showing it, so the window comes
    // forward with it.
    if (QWindow *window = backend->parentWindow()) {
        window->show();
        window->raise();
        window->requestActivate();
    }
    return true;
}

// The path a tab holds, or nothing for one that has never been named.
static QString tabPath(const QVariantMap &tab) {
    const QUrl url(tab.value(QStringLiteral("fileUrl")).toString());
    return url.isLocalFile() ? url.toLocalFile() : QString();
}

QString Remote::State() {
    QJsonArray windows;
    if (!m_windows)
        return QString::fromUtf8(QJsonDocument(QJsonObject{
            {QStringLiteral("windows"), windows}}).toJson(QJsonDocument::Compact));

    // One number across every window, so the index --tabs prints is the index
    // --read and --select take.
    int index = 0;
    const QList<Backend *> backends = m_windows->backends();
    for (Backend *backend : backends) {
        QJsonArray tabs;
        for (const QVariant &value : backend->buffers()) {
            const QVariantMap tab = value.toMap();
            const QString id = tab.value(QStringLiteral("id")).toString();
            const bool active = id == backend->activeBufferId();
            // The showing tab is read from the editor's own document; the rest
            // are read from the session, which autosave keeps within 750ms of
            // what is on screen.
            const QString text = active ? backend->currentDocumentText()
                                        : tab.value(QStringLiteral("text")).toString();
            tabs.append(QJsonObject{
                {QStringLiteral("index"), ++index},
                {QStringLiteral("id"), id},
                {QStringLiteral("title"), backend->bufferTitle(tab, index - 1)},
                {QStringLiteral("path"), tabPath(tab)},
                {QStringLiteral("active"), active},
                {QStringLiteral("modified"), tab.value(QStringLiteral("modified")).toBool()},
                {QStringLiteral("words"), Backend::countWords(text)},
                {QStringLiteral("characters"), text.length()},
                {QStringLiteral("cursor"), tab.value(QStringLiteral("cursorPosition")).toInt()},
                {QStringLiteral("firstLine"), text.section(QLatin1Char('\n'), 0, 0)}});
        }
        windows.append(QJsonObject{
            {QStringLiteral("focused"), backend->parentWindow() != nullptr
                                        && backend->parentWindow()->isActive()},
            {QStringLiteral("file"), backend->fileUrl().isLocalFile()
                                     ? backend->fileUrl().toLocalFile() : QString()},
            {QStringLiteral("tabs"), tabs}});
    }

    return QString::fromUtf8(QJsonDocument(QJsonObject{
        {QStringLiteral("windows"), windows}}).toJson(QJsonDocument::Compact));
}

// A target names a tab by the index --tabs printed, by a path, or by the tail
// of one -- a file name is what a person has to hand. Nothing names the tab
// that is showing.
Remote::Located Remote::locate(const QString &target) const {
    Located found;
    if (!m_windows)
        return found;

    const QList<Backend *> backends = m_windows->backends();
    if (target.isEmpty()) {
        Backend *backend = m_windows->primaryBackend();
        if (!backend)
            return found;
        found.backend = backend;
        found.tabId = backend->activeBufferId();
        found.active = true;
        return found;
    }

    bool isIndex = false;
    const int wanted = target.toInt(&isIndex);
    int index = 0;
    for (Backend *backend : backends) {
        for (const QVariant &value : backend->buffers()) {
            const QVariantMap tab = value.toMap();
            ++index;
            const QString path = tabPath(tab);
            const bool matches = isIndex
                ? index == wanted
                : (!path.isEmpty()
                   && (path == target
                       || QFileInfo(path).fileName() == target
                       || path.endsWith(QLatin1Char('/') + target)));
            if (!matches)
                continue;
            found.backend = backend;
            found.tabId = tab.value(QStringLiteral("id")).toString();
            found.active = found.tabId == backend->activeBufferId();
            return found;
        }
    }
    return found;
}

QString Remote::ReadText(const QString &target) {
    const Located found = locate(target);
    if (!found.backend)
        return {};

    if (found.active)
        return found.backend->currentDocumentText();

    for (const QVariant &value : found.backend->buffers()) {
        const QVariantMap tab = value.toMap();
        if (tab.value(QStringLiteral("id")).toString() == found.tabId)
            return tab.value(QStringLiteral("text")).toString();
    }
    return {};
}

bool Remote::SelectTab(const QString &target) {
    const Located found = locate(target);
    if (!found.backend || found.tabId.isEmpty())
        return false;

    if (!found.active && !found.backend->selectBuffer(found.tabId))
        return false;

    if (QWindow *window = found.backend->parentWindow()) {
        window->show();
        window->raise();
        window->requestActivate();
    }
    return true;
}
