#pragma once

#include <QObject>
#include <QString>

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

    explicit Remote(WindowManager *windows, QObject *parent = nullptr);

public slots:
    // Absolute path, a 1-based line or 0 to leave the caret alone, and whether
    // to open beside what is showing rather than over it.
    bool OpenFile(const QString &path, int line, bool newTab);

private:
    WindowManager *m_windows = nullptr;
};
