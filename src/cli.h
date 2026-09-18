#pragma once

#include <QString>
#include <QStringList>
#include <optional>

// Command line handling that runs before the GUI starts, so `omawrite --help`
// answers in the terminal instead of opening a window.
namespace Cli {

QString usage();

// What the arguments asked for. Everything but Run is answered without a
// window: the terminal is the interface for these, and a script that asked to
// append a line does not want an editor to appear.
struct Request {
    enum Kind {
        Run,       // Open the window, on `path` when there is one.
        Help,      // Usage was printed; exit with `exitCode`.
        Error,     // The arguments made no sense; exit with `exitCode`.
        Open,      // Show `path` in the running window, at `line` when given.
        Append,    // Add what is on stdin to the end of `path`.
        ListTabs,  // Print what the last session left open.
        Tabs,      // Print what a running Omawrite has open now.
        Read,      // Print the text of one of its tabs.
        Select,    // Bring one of its tabs forward.
    };

    Kind kind = Run;
    QString path;
    int line = 0;  // 1-based; 0 means the caret is left where it was.
    int exitCode = 0;
    bool newTab = false;  // --tab: open beside what is showing, not over it.
    bool json = false;    // --json: machine-readable output where it applies.
};

// FILE or FILE:LINE. The line is only taken where what follows the last colon
// is all digits, so a file whose name holds a colon still opens.
Request parseTarget(Request::Kind kind, const QString &argument, bool newTab = false);

Request parse(const QStringList &arguments);
Request parse(int argc, char *argv[]);

// Print what the last session left open, one path per line. Reads the session
// file directly: asking a running Omawrite would mean there had to be one.
int listTabs();

// Ask the running Omawrite what it has open and print it: a line per tab, or
// the raw JSON with `asJson`. 3 when nothing is running, which is a different
// answer from a running Omawrite with nothing open.
int tabs(bool asJson);

// Print the text of the tab `target` names, as the editor holds it right now.
// Nothing names the tab that is showing.
int readTab(const QString &target);

// Bring the tab `target` names forward, and its window with it.
int selectTab(const QString &target);

// Add what is on stdin to the end of `path`, creating it when it is not there.
// An Omawrite with the file open notices the change through its own watcher, so
// this needs no running instance and no window.
int appendStdin(const QString &path);

// The exit code main should return when the arguments are answered without
// starting the app, or nothing when Omawrite should open as usual.
std::optional<int> handleArguments(const QStringList &arguments);
std::optional<int> handleArguments(int argc, char *argv[]);

}
