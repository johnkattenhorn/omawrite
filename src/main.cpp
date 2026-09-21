#include <QFont>
#include <QFontDatabase>
#include <QApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QUrl>
#include <QWindow>
#include <QFile>
#include <QFileInfo>

#include "backend.h"
#include "cli.h"
#include "remote.h"
#include "systemtheme.h"
#include "windowmanager.h"
#include "workspacesession.h"

// Whether the Omawrite already on the bus took this launch. Both answers are
// final: taken means this process has nothing left to do, and not taken means
// there is nobody to take it and this process is the one that opens.
static bool startupHandedOver(const Cli::Request &request) {
    if (request.kind == Cli::Request::Open) {
        const QString absolute = QFileInfo(request.path).absoluteFilePath();
        return Remote::requestOpen(absolute, request.line, request.newTab);
    }
    return request.kind == Cli::Request::Run && Remote::requestPresent();
}

int main(int argc, char *argv[]) {
    const Cli::Request request = Cli::parse(argc, argv);
    // Answered before Qt claims the terminal, so these work over ssh and in a
    // script, with no desktop session and no window.
    switch (request.kind) {
    case Cli::Request::Help:
    case Cli::Request::Error:
        return request.exitCode;
    case Cli::Request::Append:
        return Cli::appendStdin(request.path);
    default:
        break;
    }

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("omawrite"));
    app.setDesktopFileName(QStringLiteral("omawrite"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("omawrite")));

    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/iAWriterMonoS-Regular.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/iAWriterMonoS-Italic.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/iAWriterMonoS-Bold.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/iAWriterMonoS-BoldItalic.ttf"));
    app.setOrganizationName(QStringLiteral("Omacom"));
    app.setOrganizationDomain(QStringLiteral("omacom.io"));

    if (request.kind == Cli::Request::ListTabs)
        return Cli::listTabs();

    // Answered by the Omawrite already running, so these need a bus and no
    // window of their own.
    if (request.kind == Cli::Request::Tabs)
        return Cli::tabs(request.json);
    if (request.kind == Cli::Request::Read)
        return Cli::readTab(request.path);
    if (request.kind == Cli::Request::Select)
        return Cli::selectTab(request.path);

    // An Omawrite already on this bus takes the request; a tabbed editor asked
    // to show something five times should end with five tabs at most, never
    // five windows. Nobody there means this process is the one that opens.
    //
    // A launch with no file to show asks for Omawrite, and the Omawrite already
    // running is Omawrite: it comes forward rather than being joined by a
    // second copy of itself. Two processes sharing one session file was how a
    // window record outlived the window, and an open handed to a window nobody
    // was showing went missing.
    if (startupHandedOver(request))
        return 0;

    QQuickStyle::setStyle(QStringLiteral("Material"));

    SystemTheme systemTheme(&app);

    // Carry the desktop's text scale into the default font, so the chrome that
    // inherits it (dialog titles, buttons) grows along with the writing area.
    const QFont interfaceFont(Backend::appFont());
    const qreal basePointSize = interfaceFont.pointSizeF() > 0
        ? interfaceFont.pointSizeF()
        : app.font().pointSizeF();
    const auto applyInterfaceFont = [&app, interfaceFont, basePointSize](qreal textScale) {
        QFont scaled = interfaceFont;
        scaled.setPointSizeF(basePointSize * textScale);
        app.setFont(scaled);
    };
    applyInterfaceFont(systemTheme.textScale());

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app,
                     [](const QList<QQmlError> &warnings) {
        for (const QQmlError &warning : warnings)
            qWarning().noquote() << warning.toString();
    });
    WorkspaceSession workspaceSession(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    workspaceSession.restore();
    WindowManager windows(&workspaceSession, &engine, QUrl(QStringLiteral("qrc:/Main.qml")));
    windows.setDarkMode(systemTheme.darkMode());
    windows.setTextScale(systemTheme.textScale());
    QObject::connect(&systemTheme, &SystemTheme::darkModeChanged, &windows,
                     &WindowManager::setDarkMode);
    QObject::connect(&systemTheme, &SystemTheme::textScaleChanged, &windows,
                     [&windows, applyInterfaceFont](qreal textScale) {
        applyInterfaceFont(textScale);
        windows.setTextScale(textScale);
    });

    if (windows.restoreWindows() == 0)
        windows.createWindow();
    if (!windows.primaryBackend()) {
        qCritical() << "Could not load the Omawrite interface; resource available:"
                    << QFile::exists(QStringLiteral(":/Main.qml"));
        return -1;
    }
    windows.recoverLegacySnapshots();

    // Claimed once there is a window to hand a file to, so an --open racing
    // this start is answered by a window rather than by an empty process.
    //
    // Losing the name means another Omawrite finished starting while this one
    // was building its windows. It is the one that answers from here, so hand
    // this launch to it and go, rather than staying up as a second process
    // writing the same session file.
    // The handover comes first and the windows go only once it has been taken:
    // a process that gave them up and then found nobody to hand to would be
    // left with nothing to show.
    if (!Remote::claim(&windows) && Remote::isRunning() && startupHandedOver(request)) {
        windows.abandonWindows();
        return 0;
    }

    if (!request.path.isEmpty() && !windows.primaryBackend()->modified()) {
        windows.primaryBackend()->openAtLine(
            QUrl::fromLocalFile(QFileInfo(request.path).absoluteFilePath()), request.line,
            request.newTab);
    }

    return app.exec();
}
