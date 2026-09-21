#include "cli.h"

#include "remote.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>

QString Cli::usage() {
    return QStringLiteral(
        "Omawrite is a dead-simple Markdown writing app.\n"
        "\n"
        "Usage:\n"
        "  omawrite [FILE]\n"
        "  omawrite --open FILE[:LINE] [--tab]\n"
        "  omawrite --append FILE\n"
        "  omawrite --list-tabs\n"
        "  omawrite --tabs [--json]\n"
        "  omawrite --read [TAB]\n"
        "  omawrite --select TAB\n"
        "\n"
        "Opens the Omawrite window. FILE is the Markdown file to open; without\n"
        "one, Omawrite starts on an empty document, or on the draft recovered\n"
        "from the last session. A file that is not there yet is a file you mean\n"
        "to start, so the name is taken for a blank document.\n"
        "\n"
        "Options:\n"
        "  --open FILE[:LINE]  Show FILE in the window that is already open,\n"
        "                      at LINE when one is given. Starts Omawrite when\n"
        "                      nothing is running yet.\n"
        "  --tab               Open beside what is showing rather than over it.\n"
        "  --append FILE       Add what is on standard input to the end of\n"
        "                      FILE and exit. No window opens, and a FILE that\n"
        "                      is open in a tab is updated in place.\n"
        "  --list-tabs         Print what the last session left open, one path\n"
        "                      per line, and exit.\n"
        "  --tabs              Print what the running Omawrite has open now:\n"
        "                      one line per tab, the one showing marked with\n"
        "                      *, with its word count and path. --json prints\n"
        "                      the same as JSON.\n"
        "  --read [TAB]        Print the text of TAB as the editor holds it.\n"
        "                      Without TAB, the tab that is showing.\n"
        "  --select TAB        Bring TAB forward, and its window with it.\n"
        "  TAB                 A tab, named by the number --tabs printed, by\n"
        "                      its path, or by its file name.\n"
        "  -h, --help          Show this message and exit\n"
        "\n"
        "Omawrite is a graphical app: opening a window needs a desktop session,\n"
        "and it keeps running until that window is closed. Scripts and agents\n"
        "that only want a file on screen should use --open, which returns as\n"
        "soon as the running window has it:\n"
        "\n"
        "  omawrite --open notes/standup.md:12\n"
        "  date | omawrite --append notes/log.md\n"
        "  omawrite --tabs\n"
        "  omawrite --read standup.md > /tmp/draft.md\n"
        "  omawrite --select 2\n"
        "\n"
        "Everything else is a keyboard shortcut. Ctrl+? lists them in the app,\n"
        "and https://github.com/omacom-io/omawrite#shortcuts has the same list.\n");
}

Cli::Handover Cli::handoverFor(const Request &request) {
    // Append is answered before a window is built and never reaches here.
    if (request.kind != Request::Run && request.kind != Request::Open)
        return Handover::None;
    if (!request.path.isEmpty())
        return Handover::Open;
    return request.kind == Request::Run ? Handover::Present : Handover::None;
}

Cli::Request Cli::parseTarget(Request::Kind kind, const QString &argument, bool newTab) {
    Request request;
    request.kind = kind;
    request.path = argument;
    request.newTab = newTab;

    // Only a trailing run of digits after a colon is a line number. Anything
    // else belongs to the name: a colon is legal in a filename, and guessing
    // wrong would open a file that is not there under a name nobody typed.
    const int colon = argument.lastIndexOf(QLatin1Char(':'));
    if (colon <= 0 || colon == argument.size() - 1)
        return request;

    const QString tail = argument.mid(colon + 1);
    bool numeric = false;
    const int line = tail.toInt(&numeric);
    if (!numeric || line <= 0)
        return request;

    request.path = argument.left(colon);
    request.line = line;
    return request;
}

Cli::Request Cli::parse(const QStringList &arguments) {
    Request request;

    const QStringList rest = arguments.mid(1);
    // A modifier rather than a mode, so it reads the same on either side of the
    // file it applies to.
    const bool newTab = rest.contains(QLatin1String("--tab"));
    const bool asJson = rest.contains(QLatin1String("--json"));

    for (int index = 0; index < rest.size(); ++index) {
        const QString argument = rest.at(index);

        if (argument == QLatin1String("--tab") || argument == QLatin1String("--json"))
            continue;

        if (argument == QLatin1String("-h") || argument == QLatin1String("--help")) {
            QTextStream(stdout) << usage();
            return {Request::Help, {}, 0, 0, false};
        }

        if (argument == QLatin1String("--list-tabs"))
            return {Request::ListTabs, {}, 0, 0, false};

        if (argument == QLatin1String("--tabs")) {
            Request live;
            live.kind = Request::Tabs;
            live.json = asJson;
            return live;
        }

        const bool wantsRead = argument == QLatin1String("--read");
        const bool wantsSelect = argument == QLatin1String("--select");
        if (wantsRead || wantsSelect) {
            // The target is optional for --read, which reads the tab that is
            // showing, and required for --select, which has nothing to bring
            // forward without one. A flag is never the target.
            QString target;
            if (index + 1 < rest.size()
                    && !rest.at(index + 1).startsWith(QLatin1Char('-')))
                target = rest.at(++index);

            if (wantsSelect && target.isEmpty()) {
                QTextStream(stderr) << QStringLiteral("omawrite: --select needs a tab\n\n")
                                    << usage();
                return {Request::Error, {}, 0, 1, false};
            }

            Request request;
            request.kind = wantsRead ? Request::Read : Request::Select;
            request.path = target;
            return request;
        }

        const bool wantsOpen = argument == QLatin1String("--open");
        const bool wantsAppend = argument == QLatin1String("--append");
        if (wantsOpen || wantsAppend) {
            if (index + 1 >= rest.size()) {
                QTextStream(stderr) << QStringLiteral("omawrite: %1 needs a file\n\n")
                                           .arg(argument)
                                    << usage();
                return {Request::Error, {}, 0, 1, false};
            }
            // --append takes the name whole: its text arrives on stdin, so a
            // line number would have nothing to mean.
            const QString target = rest.at(++index);
            return wantsOpen ? parseTarget(Request::Open, target, newTab)
                             : Request{Request::Append, target, 0, 0, false};
        }

        // A leading dash means an option was meant, not a file. Answering with
        // the usage beats opening a window for a file that cannot exist.
        if (argument.startsWith(QLatin1Char('-')) && argument != QLatin1String("-")) {
            QTextStream(stderr) << QStringLiteral("omawrite: unrecognized option '%1'\n\n")
                                       .arg(argument)
                                << usage();
            return {Request::Error, {}, 0, 1, false};
        }

        if (request.path.isEmpty())
            request.path = argument;
    }

    request.newTab = newTab;
    return request;
}

Cli::Request Cli::parse(int argc, char *argv[]) {
    QStringList arguments;
    arguments.reserve(argc);
    for (int index = 0; index < argc; ++index)
        arguments.append(QString::fromLocal8Bit(argv[index]));

    return parse(arguments);
}


int Cli::listTabs() {
    const QString sessionPath =
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
            .filePath(QStringLiteral("session.json"));

    QFile session(sessionPath);
    if (!session.open(QIODevice::ReadOnly)) {
        // Nothing open is not a failure: a first run has no session yet, and a
        // caller looping over the output should see an empty list, not an error.
        return 0;
    }

    const QJsonDocument document = QJsonDocument::fromJson(session.readAll());
    const QJsonObject root = document.object();

    // Windows hold tabs when a workspace wrote the file; a single window writes
    // its buffers at the top level. Read both rather than guess which ran last.
    QJsonArray entries = root.value(QStringLiteral("buffers")).toArray();
    for (const QJsonValue &window : root.value(QStringLiteral("windows")).toArray()) {
        for (const QJsonValue &tab : window.toObject().value(QStringLiteral("tabs")).toArray())
            entries.append(tab);
    }

    QTextStream out(stdout);
    for (const QJsonValue &entry : entries) {
        const QString fileUrl = entry.toObject().value(QStringLiteral("fileUrl")).toString();
        const QUrl url(fileUrl);
        out << (url.isLocalFile() ? url.toLocalFile() : QStringLiteral("(untitled)"))
            << QLatin1Char('\n');
    }
    return 0;
}

// One line per tab: the marker for the tab showing, the number that names it
// everywhere else, its word count, and where it lives.
static QString tabLine(const QJsonObject &tab) {
    const QString path = tab.value(QStringLiteral("path")).toString();
    const int words = tab.value(QStringLiteral("words")).toInt();
    return QStringLiteral("%1 %2  %3  %4 words%5")
        .arg(tab.value(QStringLiteral("active")).toBool() ? QStringLiteral("*")
                                                          : QStringLiteral(" "))
        .arg(tab.value(QStringLiteral("index")).toInt(), 2)
        .arg(path.isEmpty() ? tab.value(QStringLiteral("title")).toString() : path)
        .arg(words)
        .arg(tab.value(QStringLiteral("modified")).toBool() ? QStringLiteral("  modified")
                                                            : QString());
}

int Cli::tabs(bool asJson) {
    const QString state = Remote::requestState();
    if (state.isEmpty()) {
        // An Omawrite that holds the name but will not answer is an older one
        // than this: the name was there before these calls were.
        QTextStream(stderr) << (Remote::isRunning()
            ? QStringLiteral("omawrite: the running Omawrite is older than this "
                             "one and has no --tabs to answer with; restart it\n")
            : QStringLiteral("omawrite: nothing running on this session bus; "
                             "--list-tabs reads the last session instead\n"));
        return 3;
    }

    QTextStream out(stdout);
    if (asJson) {
        out << state << QLatin1Char('\n');
        return 0;
    }

    const QJsonArray windows = QJsonDocument::fromJson(state.toUtf8())
                                   .object()
                                   .value(QStringLiteral("windows"))
                                   .toArray();
    int number = 0;
    for (const QJsonValue &value : windows) {
        const QJsonObject window = value.toObject();
        out << QStringLiteral("window %1%2\n")
                   .arg(++number)
                   .arg(window.value(QStringLiteral("focused")).toBool()
                            ? QStringLiteral(" (focused)") : QString());
        for (const QJsonValue &tab : window.value(QStringLiteral("tabs")).toArray())
            out << QStringLiteral("  ") << tabLine(tab.toObject()) << QLatin1Char('\n');
    }
    return 0;
}

int Cli::readTab(const QString &target) {
    const QString text = Remote::requestText(target);
    // A tab can hold nothing at all, so an empty answer only means failure
    // when nobody answered. Asking what is open tells the two apart.
    if (text.isEmpty() && Remote::requestState().isEmpty()) {
        QTextStream(stderr) << (Remote::isRunning()
            ? QStringLiteral("omawrite: the running Omawrite is older than this "
                             "one and has no --read to answer with; restart it\n")
            : QStringLiteral("omawrite: nothing running on this session bus\n"));
        return 3;
    }

    QTextStream out(stdout);
    out << text;
    if (!text.endsWith(QLatin1Char('\n')))
        out << QLatin1Char('\n');
    return 0;
}

int Cli::selectTab(const QString &target) {
    if (Remote::requestSelect(target))
        return 0;

    QTextStream(stderr) << QStringLiteral("omawrite: no tab called %1 is open\n").arg(target);
    return 4;
}

int Cli::appendStdin(const QString &path) {
    QTextStream in(stdin);
    const QString text = in.readAll();

    const QString absolute = QFileInfo(path).absoluteFilePath();
    QFile file(absolute);
    if (!file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream(stderr) << QStringLiteral("omawrite: could not append to %1\n").arg(absolute);
        return 1;
    }

    // A file that does not end in a newline would otherwise have the new text
    // run on from its last line, which is never what appending a note means.
    if (file.size() > 0) {
        QFile existing(absolute);
        if (existing.open(QIODevice::ReadOnly)) {
            existing.seek(qMax(qint64(0), existing.size() - 1));
            if (existing.read(1) != QByteArray("\n"))
                file.write("\n");
        }
    }

    file.write(text.toUtf8());
    if (!text.endsWith(QLatin1Char('\n')))
        file.write("\n");
    return file.flush() ? 0 : 1;
}

std::optional<int> Cli::handleArguments(const QStringList &arguments) {
    const Request request = parse(arguments);
    if (request.kind == Request::Help || request.kind == Request::Error)
        return request.exitCode;

    return std::nullopt;
}

std::optional<int> Cli::handleArguments(int argc, char *argv[]) {
    QStringList arguments;
    arguments.reserve(argc);
    for (int index = 0; index < argc; ++index)
        arguments.append(QString::fromLocal8Bit(argv[index]));

    return handleArguments(arguments);
}
