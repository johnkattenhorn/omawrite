#pragma once

#include <QObject>
#include <QString>

class Backend;
class WindowManager;

// The session-bus face of a running Omawrite, so `omawrite --open` reaches the
// window already on screen instead of starting a second one beside it.
//
// Without this every open is a new process and a new window, which is the wrong
// answer for a tabbed editor: a script that shows a file five times should leave
// five tabs at most, not five windows.
class Remote : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.omacom.Omawrite")

public:
    static QString serviceName();
    static QString objectPath();

    // True when this process now owns the name, which also means no other
    // Omawrite was holding it.
    static bool claim(WindowManager *windows);

    // Ask the Omawrite that owns the name to show this file. False when nobody
    // answered, which is the caller's signal to open a window itself.
    static bool requestOpen(const QString &path, int line, bool newTab);

    // Whether an Omawrite holds the name on this session bus at all, which is
    // a different answer from one that is there and would not say.
    static bool isRunning();

    // What the running Omawrite has open, as JSON, live from the windows
    // themselves rather than from the session file the last save left behind.
    // Empty when nothing answered.
    static QString requestState();

    // The text of one tab as the editor holds it right now, and the tab a
    // caller wants brought forward. Both take the same target: a 1-based index
    // as --tabs prints it, a path, a file name, or nothing for the tab that is
    // showing. Empty (false) when nothing answered.
    static QString requestText(const QString &target);
    static bool requestSelect(const QString &target);

    explicit Remote(WindowManager *windows, QObject *parent = nullptr);

public slots:
    // Absolute path, a 1-based line or 0 to leave the caret alone, and whether
    // to open beside what is showing rather than over it.
    bool OpenFile(const QString &path, int line, bool newTab);

    // A JSON snapshot of every window and tab, the active one marked, with the
    // text of the tab that is showing measured from the editor's own document.
    QString State();

    // The text of the tab `target` names, empty when it names none.
    QString ReadText(const QString &target);

    // Bring the tab `target` names forward, and its window with it.
    bool SelectTab(const QString &target);

private:
    // The tab a target names, as the window holding it and the tab's id.
    struct Located {
        Backend *backend = nullptr;
        QString tabId;
        bool active = false;
    };
    Located locate(const QString &target) const;

    WindowManager *m_windows = nullptr;
};
